/* taperecordtest.c: recording completion failures with the real recorder
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
#include <string.h>
#include "libspectrum.h"
#include "fuse.h"
#include "fuse-internal.h"
#include "tape.h"
#include "tape_internals.h"
#include "peripherals/ula.h"
#include "ui/ui.h"

static libspectrum_error controlled_append( libspectrum_tape *target,
                                            libspectrum_tape_block *block );
static int recording_menu( ui_menu_item item, int active );
/* Only this test's recorder translation unit substitutes these services.
   No production injection hooks; the rest of the archive is unchanged. */
#define libspectrum_tape_append_block controlled_append
#define ui_menu_activate recording_menu
#include "../tape_record.c"
#undef libspectrum_tape_append_block
#undef ui_menu_activate

#undef NDEBUG
#include <assert.h>

static unsigned char rom_bytes[0x4000];
static int append_calls, fail_at, inside_operation, menu_calls;

static void
check_busy( void )
{
  fuse_tape_info *info = (fuse_tape_info *)1;
  if( !inside_operation ) return;
  assert( fuse_host_copy_tape_info( &info ) == FUSE_HOST_OPERATION_BUSY && !info );
  assert( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_stop_tape_recording() == FUSE_HOST_OPERATION_BUSY );
}

static libspectrum_error
controlled_append( libspectrum_tape *target, libspectrum_tape_block *block )
{
  assert( fuse_emulation_paused == 2 );
  check_busy();
  if( ++append_calls == fail_at ) return LIBSPECTRUM_ERROR_UNKNOWN;
  return libspectrum_tape_append_block( target, block );
}

static int
recording_menu( ui_menu_item item, int active )
{
  assert( fuse_emulation_paused == 2 );
  assert( item == UI_MENU_ITEM_TAPE_RECORDING );
  assert( !!tape_recording == !!active );
  check_busy(); menu_calls++;
  return ui_menu_activate( item, active );
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  (void)context;
  assert( !strcmp( name, "48.rom" ) );
  rom->data = rom_bytes; rom->length = sizeof(rom_bytes); rom->custom = 0;
  return 0;
}

static int
recording_call( int host, int start )
{
  int result;
  inside_operation = host;
  result = host ? ( start ? fuse_host_start_tape_recording() : fuse_host_stop_tape_recording() )
                : ( start ? tape_record_start() : tape_record_stop() );
  inside_operation = 0;
  return result;
}

static int
block_count( void )
{
  libspectrum_tape_iterator iterator;
  libspectrum_tape_block *block;
  int count = 0;
  for( block = libspectrum_tape_iterator_init( &iterator, tape ); block;
       block = libspectrum_tape_iterator_next( &iterator ) ) count++;
  return count;
}

static void
check_failure( int host, int failed_append )
{
  fuse_media_decision_id request;
  int kept = failed_append - 1;
  assert( !tape_present() && !tape_recording );
  tape_modified = 0; append_calls = 0; fail_at = failed_append;
  assert( !recording_call( host, 1 ) );
  tstates += 17; tape_record_edge( !ula_tape_level() ); tstates += 23;
  assert( recording_call( host, 0 ) ==
          ( host ? FUSE_HOST_OPERATION_MEDIA_ERROR : LIBSPECTRUM_ERROR_UNKNOWN ) );
  assert( !tape_recording && block_count() == kept && !!tape_modified == !!kept );
  assert( append_calls == failed_append );
  fail_at = 0;
  assert( !recording_call( host, 0 ) ); /* No tail retry or duplicate blocks. */
  assert( block_count() == kept && append_calls == failed_append );
  assert( fuse_emulation_paused == 2 && fuse_run_tstates( 1 ) == FUSE_RUN_PAUSED );
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  assert( !recording_call( host, 1 ) && !recording_call( host, 1 ) );
  tstates += 7;
  assert( !recording_call( host, 0 ) && block_count() == kept + 1 );
  assert( fuse_host_save_tape_file( "recovered.pzx" ) == FUSE_HOST_OPERATION_OK );
  assert( !tape_modified );
  assert( fuse_host_request_tape_eject( &request ) == FUSE_HOST_OPERATION_OK && !request );
  assert( fuse_host_insert_tape_file( "recovered.pzx", &request ) == FUSE_HOST_OPERATION_OK );
  assert( tape_present() );
  assert( fuse_host_request_tape_eject( &request ) == FUSE_HOST_OPERATION_OK && !request );
  assert( !remove( "recovered.pzx" ) );
}

int
main( void )
{
  fuse_host_resources resources = { NULL, acquire_rom };
  fuse_host_config *config = fuse_host_config_alloc();
  int host, failure;
  assert( !fuse_host_config_set_machine( config, "48" ) );
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( !fuse_host_config_set_tape_traps( config, 0 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  for( host = 0; host <= 1; host++ ) {
    for( failure = 1; failure <= 2; failure++ ) {
      /* Inject elapsed intervals while held; operations must preserve nesting. */
      check_failure( host, failure );
    }
  }
  assert( menu_calls > 0 && fuse_emulation_paused == 2 );
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  fuse_shutdown();
  puts( "Recording stop failures retain partial blocks, end recording and admit subsequent work" );
  return 0;
}
