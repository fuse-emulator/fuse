/* snapshotfiletest.c: chosen-target snapshot save and legacy workflow
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compat.h"
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-legacy.h"
#include "memory_pages.h"
#include "module.h"
#include "rzx.h"
#include "settings.h"
#include "snapshot.h"
#include "z80/z80.h"
#undef NDEBUG
#include <assert.h>

/* Track libspectrum allocations only during a save. This fresh-process fixture
   uses libc-compatible allocators; pre-existing core allocations are ignored. */
static void *allocations[4096];
static size_t allocation_count;
static int malformed_export, in_save, in_host_save, expected_pause;
static compat_file_test_hooks normal_io;
static int io_fault, io_closes;

static void
check_save_callback( void )
{
  if( !in_save ) return;
  assert( fuse_emulation_paused == expected_pause );
  if( in_host_save ) {
    assert( fuse_host_save_snapshot_file( "not-written.szx" ) == FUSE_HOST_OPERATION_BUSY );
    assert( fuse_host_open_file( "not-opened.szx", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_BUSY );
    assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == FUSE_HOST_OPERATION_BUSY );
    assert( fuse_run_frame() == FUSE_RUN_BUSY );
  }
}

static void
export_failure( libspectrum_snap *snap )
{
  check_save_callback();
  /* Exercise a real writer error after it has emitted header/CPU chunks. */
  if( malformed_export ) libspectrum_snap_set_opus_active( snap, 1 );
}
static module_info_t export_module = { NULL, NULL, NULL, NULL, export_failure, NULL };

static void
remember( void *pointer )
{
  if( !pointer ) return;
  assert( allocation_count < sizeof(allocations)/sizeof(allocations[0]) );
  allocations[allocation_count++] = pointer;
}

static void
forget( void *pointer )
{
  size_t i;
  for( i = 0; i < allocation_count; i++ )
    if( allocations[i] == pointer ) {
      allocations[i] = allocations[--allocation_count];
      return;
    }
}

static void *tracked_malloc( size_t size )
{
  void *pointer = malloc( size );
  assert( pointer || !size ); remember( pointer ); return pointer;
}

static void *tracked_calloc( size_t count, size_t size )
{
  void *pointer = calloc( count, size );
  assert( pointer || !count || !size ); remember( pointer ); return pointer;
}

static void *tracked_realloc( void *pointer, size_t size )
{
  void *result;
  forget( pointer ); result = realloc( pointer, size );
  assert( result || !size ); remember( result ); return result;
}

static void tracked_free( void *pointer )
{
  forget( pointer ); free( pointer );
}

static void
compare_file( const char *filename, const unsigned char *expected, size_t length )
{
  FILE *file = fopen( filename, "rb" );
  unsigned char *actual = malloc( length );
  assert( file && actual );
  assert( fread( actual, 1, length, file ) == length && fgetc( file ) == EOF );
  assert( !memcmp( actual, expected, length ) );
  assert( !fclose( file ) && !remove( filename ) ); free( actual );
}

static int
save_file( int host, const char *filename )
{
  int error, paused = fuse_emulation_paused;
  expected_pause = paused + 1; in_save = 1; in_host_save = host;
  if( host ) {
    fuse_host_operation_result result = fuse_host_save_snapshot_file( filename );
    assert( result == FUSE_HOST_OPERATION_OK || result == FUSE_HOST_OPERATION_MEDIA_ERROR );
    error = result != FUSE_HOST_OPERATION_OK;
  } else {
    /* Chosen-target portion of menu_file_savesnapshot, without chooser/free. */
    fuse_emulation_pause(); error = snapshot_write( filename ); fuse_emulation_unpause();
  }
  in_save = in_host_save = 0;
  assert( fuse_emulation_paused == paused && allocation_count == 0 );
  return !!error;
}

static int
controlled_write( compat_fd fd, const unsigned char *buffer, size_t length, size_t *put )
{
  check_save_callback();
  if( io_fault == 1 ) {
    assert( length > 7 && !normal_io.write( fd, buffer, 7, put ) );
    return 37;
  }
  return normal_io.write( fd, buffer, length, put );
}

static int
controlled_close( compat_fd fd )
{
  int error;
  check_save_callback(); io_closes++;
  error = normal_io.close( fd );
  return io_fault == 2 ? 37 : error;
}

int
snapshot_file_test( int argc, char **argv )
{
  libspectrum_mem_vtable_t tracking = {
    tracked_malloc, tracked_calloc, tracked_realloc, tracked_free
  };
  libspectrum_mem_vtable_t normal = { malloc, calloc, realloc, free };
  libspectrum_snap *snap;
  unsigned char *expected = NULL, *z80_expected = NULL;
  size_t length = 0, z80_length = 0;
  int flags = 0, host, nested;
  FILE *file;
  compat_file_test_hooks failing_io;
  assert( fuse_legacy_prepare( argc, argv ) == FUSE_INIT_READY );
  assert( !module_register( &export_module ) );
  assert( fuse_host_save_snapshot_file( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_snapshot_file( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  z80.pc.w = 0x8000; z80.sp.w = 0x9000;
  writebyte_internal( 0x8100, 0x5a );
  snap = libspectrum_snap_alloc();
  assert( !snapshot_copy_to( snap ) );
  assert( !libspectrum_snap_write( &expected, &length, &flags, snap,
                                  LIBSPECTRUM_ID_SNAPSHOT_SZX, fuse_creator, 0 ) );
  assert( !libspectrum_snap_write( &z80_expected, &z80_length, &flags, snap,
                                  LIBSPECTRUM_ID_SNAPSHOT_Z80, fuse_creator, 0 ) );
  assert( !libspectrum_snap_free( snap ) );
  compat_file_test_get_hooks( &normal_io ); failing_io = normal_io;
  failing_io.write = controlled_write; failing_io.close = controlled_close;
  for( host = 0; host <= 1; host++ ) for( nested = 0; nested <= 1; nested++ ) {
    if( nested ) assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
    libspectrum_mem_set_vtable( &tracking );
    file = fopen( "saved.szx", "wb" ); assert( file );
    assert( fputs( "old target", file ) >= 0 && !fclose( file ) );
    assert( !save_file( host, "saved.szx" ) ); /* Existing target is overwritten. */
    compare_file( "saved.szx", expected, length );
    assert( !save_file( host, "fallback.unknown" ) );
    compare_file( "fallback.unknown", expected, length );
    assert( !save_file( host, "fallback.tap" ) ); /* Non-snapshot class also uses SZX. */
    compare_file( "fallback.tap", expected, length );
    assert( !save_file( host, "saved.z80" ) );
    compare_file( "saved.z80", z80_expected, z80_length );
    malformed_export = 1; /* Active Opus with no ROM: SZX returns partial output. */
    assert( save_file( host, "partial.szx" ) );
    file = fopen( "partial.szx", "rb" ); assert( !file );
    malformed_export = 0;
    z80.sp.w = 0; /* SNA cannot stack PC here; codec fails before touching target. */
    file = fopen( "failed.sna", "wb" ); assert( file );
    assert( fputs( "keep", file ) >= 0 && !fclose( file ) );
    assert( save_file( host, "failed.sna" ) );
    compare_file( "failed.sna", (const unsigned char *)"keep", 4 );
    z80.sp.w = 0x9000;
    assert( save_file( host, "missing-directory/failed.szx" ) );
    compat_file_test_set_hooks( &failing_io );
    io_fault = 1; io_closes = 0;
    assert( save_file( host, "short.szx" ) && io_closes == 1 );
    compare_file( "short.szx", expected, 7 ); /* No invented output rollback. */
    io_fault = 2; io_closes = 0;
    assert( save_file( host, "close.szx" ) && io_closes == 1 );
    compare_file( "close.szx", expected, length ); /* Complete bytes can still fail. */
    io_fault = 0; compat_file_test_set_hooks( &normal_io );
    assert( !save_file( host, "recovered.szx" ) );
    compare_file( "recovered.szx", expected, length );
    assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && readbyte_internal(0x8100) == 0x5a );
    libspectrum_mem_set_vtable( &normal );
    if( nested ) assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  }
  libspectrum_free( expected ); libspectrum_free( z80_expected );
  /* First pause has existing competition-RZX side effects even if Save fails;
     non-competition recording is not stopped by snapshot save. */
  for( host = 0; host <= 1; host++ ) {
    settings_current.competition_mode = 0;
    assert( !rzx_start_recording( "snapshot.rzx", 0 ) && rzx_recording );
    assert( save_file( host, "missing-directory/failed.szx" ) && rzx_recording );
    assert( !rzx_stop_recording() && !remove( "snapshot.rzx" ) );
    settings_current.competition_mode = 1;
    assert( !rzx_start_recording( "competition.rzx", 0 ) && rzx_recording );
    assert( save_file( host, "missing-directory/failed.szx" ) && !rzx_recording );
    if( compat_file_exists( "competition.rzx" ) ) assert( !remove( "competition.rzx" ) );
  }
  settings_current.competition_mode = 0;
  fuse_shutdown();
  return 0;
}
