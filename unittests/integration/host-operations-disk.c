/* host-operations-disk.c: host operation integration tests: disk
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "host-operations.h"

void
write_disk_fixture( const char *filename )
{
  disk_t disk = { 0 };
  assert( disk_new( &disk, 1, 40, DISK_DD, DISK_UDI ) == DISK_OK );
  DISK_SET_TRACK( &disk, 0, 0 );
  disk.i = 0; disk.track[0] = 0x5a;
  assert( disk_write( &disk, filename ) == DISK_OK );
  disk_close( &disk );
}

static int open_disk_autoloads;

static int
count_open_disk_autoload( void )
{
  const disk_drive_info_t *drive = disk_drive_find( FUSE_DISK_CONTROLLER_PLUS3, 0 );
  assert( drive && drive->fdd->loaded );
  open_disk_autoloads++;
  return 0;
}

extern int null_ui_query_calls;

void
check_floppy_merge( void )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  const disk_drive_info_t *drive = disk_drive_find( controller, 0 );
  const char *side_a = "merge Side A.udi", *side_b = "merge Side B.udi";
  fuse_open_request request;
  fuse_media_decision_id old;

  settings_set_string( &settings_current.disk_try_merge, "Always" );
  settings_current.disk_ask_merge = 1;
  assert( fuse_host_select_machine( "plus3" ) == OK );
  write_disk_fixture( side_a );
  write_disk_fixture( side_b );
  assert( fuse_host_insert_disk_file( controller, 0, side_a, &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MERGE_FLOPPY &&
          request.detail.floppy.slot == 0 );
  assert( !drive->fdd->loaded && fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_disk_merge( request.id,
                                       2 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !remove( side_a ) );
  assert( fuse_host_answer_disk_merge( request.id, 1 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 2 );
  write_disk_fixture( side_a );
  drive->fdd->disk.dirty = 1;
  assert( fuse_host_open_file( side_a,
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_SAVE_FLOPPY );
  old = request.id;
  assert( fuse_host_answer_disk_merge( old, 1 ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_disk_eject( old, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                       &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.id != old && request.question == FUSE_OPEN_MERGE_FLOPPY );
  assert( !drive->fdd->loaded &&
          fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_disk_merge( old, 1 ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_disk_merge( request.id, 0 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 1 );
  assert( !remove( side_b ) );
  assert( fuse_host_open_file( side_a,
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MERGE_FLOPPY );
  assert( fuse_host_answer_disk_merge( request.id, 1 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 1 );
  assert( !null_ui_query_calls );
  assert( !disk_drive_insert( drive, side_a, 0 ) );
  assert( null_ui_query_calls == 1 && !remove( side_a ) );
}

void
check_open_floppy( void )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  disk_drive_info_t *drive = disk_drive_find( controller, 0 );
  disk_drive_info_t *other = disk_drive_find( controller, 1 );
  disk_drive_autoload_hook_fn original_autoload = drive->autoload_hook;
  fuse_open_request request;
  int rom_requests;
  int autoload = tape_can_autoload();
  unsigned char dsk[1024] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
  dsk[0x30] = dsk[0x31] = 1; /* One track, one side. */
  dsk[0x33] = 3; /* 768 bytes per track, including the header. */
  memcpy( dsk + 256, "Track-Info\r\n", 12 );
  dsk[0x114] = dsk[0x11b] = 2; /* 512-byte sector. */
  dsk[0x115] = 1;
  dsk[0x11a] = 1;
  assert( !utils_write_file( "incoming.dsk", dsk, sizeof( dsk ) ) );

  assert( fuse_host_select_machine( "plus3" ) == OK );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( new_disk_without_decision( controller, 1 ) == OK );
  assert( fuse_host_select_machine( "48" ) == OK );
  drive->autoload_hook = count_open_disk_autoload;

  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.id && request.question == FUSE_OPEN_SAVE_FLOPPY );
  assert( request.detail.floppy.controller == controller &&
          request.detail.floppy.slot == 0 );
  assert( !request.detail.floppy.has_save_target );
  assert( machine_current->machine == LIBSPECTRUM_MACHINE_PLUS3 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !open_disk_autoloads );
  rom_requests = provider.acquired;
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_CANCEL,
                                       NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( machine_current->machine == LIBSPECTRUM_MACHINE_PLUS3 && provider.acquired == rom_requests );
  assert( drive->fdd->loaded && drive->fdd->disk.dirty && !drive->fdd->disk.filename );
  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_SAVE,
                                       "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( drive->fdd->loaded && drive->fdd->disk.dirty && !open_disk_autoloads );
  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.dsk" ) );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
  assert( provider.acquired == rom_requests && open_disk_autoloads == autoload );
  assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "incoming.dsk" ) );
  assert( other->fdd->loaded && other->fdd->disk.dirty && !other->fdd->disk.filename );
  write_disk_fixture( "named-open.udi" );
  drive->fdd->disk.dirty = 1;
  assert( fuse_host_open_file( "named-open.udi",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_SAVE_FLOPPY &&
          request.detail.floppy.has_save_target );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_CANCEL,
                                       NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( !remove( "named-open.udi" ) );
  drive->autoload_hook = original_autoload;
}

void
check_lifecycle_disk_file( void )
{
  disk_t disk = { 0 };
  disk_drive_info_t *drive = disk_drive_find( FUSE_DISK_CONTROLLER_PLUS3, 0 );
  FILE *actual_file, *expected_file;
  int actual_byte, expected_byte;
  /* Reference the full workflow, including the +3 insertion hook's preformat.
     The chosen-target Save path already has independent chooser equivalence. */
  assert( drive && !drive->fdd->loaded );
  assert( !disk_drive_insert( drive, NULL, 0 ) );
  assert( !disk_drive_flip( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) );
  assert( !disk_drive_flip( FUSE_DISK_CONTROLLER_PLUS3, 0, 0 ) );
  assert( !disk_drive_writeprotect( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) );
  assert( !disk_drive_writeprotect( FUSE_DISK_CONTROLLER_PLUS3, 0, 0 ) );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, "expected-lifecycle.udi" ) == OK );
  assert( !disk_drive_eject( FUSE_DISK_CONTROLLER_PLUS3, 0 ) );
  assert( disk_open( &disk, "lifecycle.udi", 0, 0 ) == DISK_OK );
  assert( disk.type == DISK_UDI && disk.sides == drive->get_params()->heads &&
          disk.cylinders == drive->get_params()->cylinders );
  disk_close( &disk );
  actual_file = fopen( "lifecycle.udi", "rb" );
  expected_file = fopen( "expected-lifecycle.udi", "rb" );
  assert( actual_file && expected_file );
  do {
    actual_byte = fgetc( actual_file ); expected_byte = fgetc( expected_file );
    assert( actual_byte == expected_byte );
  } while( actual_byte != EOF );
  assert( !ferror( actual_file ) && !ferror( expected_file ) );
  assert( !fclose( actual_file ) && !fclose( expected_file ) );
  assert( !remove( "expected-lifecycle.udi" ) );
}

static int (*disk_available)( void );
static int disk_available_calls;

static int
check_disk_available( void )
{
  /* Even legacy lookup/availability runs inside host-owned admission. */
  assert( fuse_engine_check_operation() == FUSE_HOST_OPERATION_BUSY );
  check_disk_info_blocked( FUSE_HOST_OPERATION_BUSY );
  assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_flipped( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_write_protected( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_BUSY );
  disk_available_calls++;
  return disk_available();
}

void
check_disk_save( fuse_disk_controller controller )
{
  disk_drive_info_t *drive = disk_drive_find( controller, 0 );
  disk_t check = { 0 };
  char output[] = "api-copy.udi";
  int selected, ready, loadhead, length;
  fdd_type_t type;
  fuse_disk_drive_info *info = NULL;
  assert( drive && drive->fdd && drive->is_available );
  disk_available = drive->is_available;
  drive->is_available = check_disk_available;
  assert( fuse_host_copy_disk_drive_info( controller, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_disk_drive_info( (fuse_disk_controller)99, 0, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, -1, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, 99, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, 0, &info ) == OK );
  assert( info->controller == controller && !info->slot && info->available && info->enabled && !info->loaded );
  assert( !strcmp( info->name, drive->name ) && info->name != drive->name && !info->filename );
  fuse_disk_drive_info_free( info ); info = NULL;
  assert( eject_disk_without_decision( controller, 0 ) == OK );
  assert( fuse_host_set_disk_flipped( controller, 0, 1 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_set_disk_write_protected( controller, 0, 1 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_set_disk_flipped( controller, 0, 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_set_disk_write_protected( controller, 0, -1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( new_disk_without_decision( (fuse_disk_controller)99, 0 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( eject_disk_without_decision( controller, 99 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  for( length = 0; length < 20; length++ ) {
    utils_file truncated;
    utils_file_init( &truncated, "short-header.udi" );
    truncated.length = length;
    truncated.buffer = libspectrum_new0( unsigned char, length ? length : 1 );
    truncated.type = LIBSPECTRUM_ID_DISK_UDI;
    assert( disk_open_loaded( &check, &truncated, 0, 0 ) == DISK_OPEN );
    disk_close( &check ); utils_file_free( &truncated );
  }
  assert( fuse_host_save_disk_file( controller, 0, "not-created.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !compat_file_exists( "not-created.udi" ) );
  assert( fuse_host_save_disk_file( (fuse_disk_controller)99, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, -1, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, 99, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, 0, "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( insert_disk_file_without_decision( controller, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( insert_disk_file_without_decision( controller, 0, "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  if( controller == FUSE_DISK_CONTROLLER_PLUS3 ) {
    assert( fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_BETA, 0, &info ) == OK );
    assert( !info->available ); fuse_disk_drive_info_free( info ); info = NULL;
    assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_BETA, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_BETA, 0, "not-created.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_BETA, 0 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_BETA, 0 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  }
  write_disk_fixture( "api-input.udi" );
  type = drive->fdd->type; drive->fdd->type = FDD_TYPE_NONE;
  assert( fuse_host_copy_disk_drive_info( controller, 0, &info ) == OK );
  assert( info->available && !info->enabled ); fuse_disk_drive_info_free( info ); info = NULL;
  assert( insert_disk_file_without_decision( controller, 0, "api-input.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !drive->fdd->loaded );
  drive->fdd->type = type;
  assert( insert_disk_file_without_decision( controller, 0, "api-input.udi" ) == OK );
  assert( fuse_host_save_disk_file( controller, 0, "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "api-input.udi" ) );
  assert( fuse_host_save_disk_file( controller, 0, output ) == OK );
  output[0] = 'X';
  assert( !strcmp( drive->fdd->disk.filename, "api-copy.udi" ) );
  /* Real registered controller/FDD data, with deterministic write preconditions. */
  selected = drive->fdd->selected; ready = drive->fdd->ready; loadhead = drive->fdd->loadhead;
  drive->fdd->selected = drive->fdd->ready = drive->fdd->loadhead = 1;
  DISK_SET_TRACK( &drive->fdd->disk, 0, 0 );
  drive->fdd->disk.i = 0; drive->fdd->data = 0x6b; drive->fdd->marks = 0;
  assert( fdd_write_data( drive->fdd ) == FDD_OK && drive->fdd->disk.dirty );
  drive->fdd->selected = selected; drive->fdd->ready = ready; drive->fdd->loadhead = loadhead;
  assert( fuse_host_save_disk_file( controller, 0, NULL ) == OK );
  assert( !drive->fdd->disk.dirty );
  assert( fuse_host_copy_disk_drive_info( controller, 0, &retained_disk_info ) == OK );
  assert( retained_disk_info->loaded && !strcmp( retained_disk_info->filename, "api-copy.udi" ) );
  assert( retained_disk_info->filename != drive->fdd->disk.filename );
  assert( disk_open( &check, "api-copy.udi", 0, 0 ) == DISK_OK );
  DISK_SET_TRACK( &check, 0, 0 ); assert( check.track[0] == 0x6b ); disk_close( &check );
  assert( disk_open( &check, "api-input.udi", 0, 0 ) == DISK_OK );
  DISK_SET_TRACK( &check, 0, 0 ); assert( check.track[0] == 0x5a ); disk_close( &check );
  assert( fuse_host_set_disk_flipped( controller, 0, 1 ) == OK && drive->fdd->upsidedown );
  assert( fuse_host_set_disk_flipped( controller, 0, 0 ) == OK && !drive->fdd->upsidedown );
  assert( fuse_host_set_disk_write_protected( controller, 0, 1 ) == OK );
  assert( drive->fdd->wrprot && !drive->fdd->disk.dirty );
  drive->fdd->selected = drive->fdd->ready = drive->fdd->loadhead = 1;
  DISK_SET_TRACK( &drive->fdd->disk, 0, 0 );
  drive->fdd->disk.i = 0; drive->fdd->data = 0x99;
  assert( fdd_write_data( drive->fdd ) == FDD_RDONLY );
  assert( drive->fdd->disk.track[0] == 0x6b && !drive->fdd->disk.dirty );
  drive->fdd->selected = selected; drive->fdd->ready = ready; drive->fdd->loadhead = loadhead;
  assert( fuse_host_set_disk_write_protected( controller, 0, 0 ) == OK && !drive->fdd->wrprot );
  assert( eject_disk_without_decision( controller, 0 ) == OK && !drive->fdd->loaded );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( drive->fdd->loaded && !drive->fdd->disk.filename && drive->fdd->disk.dirty );
  assert( drive->fdd->disk.sides == drive->get_params()->heads &&
          drive->fdd->disk.cylinders == drive->get_params()->cylinders );
  {
    fuse_media_decision_id request, other;
    fuse_media_decision_id tape_request;
    int has_second = controller == FUSE_DISK_CONTROLLER_PLUS3;
    if( has_second )
      assert( insert_disk_file_without_decision( controller, 1, "api-input.udi" ) == OK );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( fuse_host_eject_disk( controller, 0, &other ) == FUSE_HOST_OPERATION_NEEDS_DECISION && other == request );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION &&
            fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_host_request_tape_eject( &tape_request ) == FUSE_HOST_OPERATION_BUSY && !tape_request );
    assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == FUSE_HOST_OPERATION_STALE );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_DISCARD, "unused.udi" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
    if( has_second )
      assert( fuse_host_eject_disk( controller, 1, &other ) == FUSE_HOST_OPERATION_BUSY && !other );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_CANCEL, NULL ) == FUSE_HOST_OPERATION_CANCELLED );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                         "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( fuse_host_save_disk_file( controller, 0, "api-eject.udi" ) == OK );
    assert( new_disk_without_decision( controller, 0 ) == OK );
    assert( answer_disk_eject_without_followup( request,
                                                FUSE_MEDIA_DECISION_DISCARD,
                                                NULL ) ==
            FUSE_HOST_OPERATION_STALE );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE, "api-eject.udi" ) == OK );
    assert( !drive->fdd->loaded && !remove( "api-eject.udi" ) );
    {
      char path[] = "api-staged.udi";
      libspectrum_byte *outgoing;
      assert( new_disk_without_decision( controller, 0 ) == OK );
      outgoing = drive->fdd->disk.data;
      assert( insert_disk_file_without_decision( controller, 0, "missing.udi" ) ==
              FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( drive->fdd->loaded && drive->fdd->disk.data == outgoing );
      write_disk_fixture( path );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
      assert( new_disk_without_decision( controller, 0 ) == FUSE_HOST_OPERATION_BUSY );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_CANCEL, NULL ) ==
              FUSE_HOST_OPERATION_CANCELLED );
      assert( drive->fdd->loaded && drive->fdd->disk.data == outgoing );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                           "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( drive->fdd->loaded && drive->fdd->disk.dirty && drive->fdd->disk.data == outgoing );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( !remove( path ) ); path[0] = 'X';
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
      assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "api-staged.udi" ) );
      assert( new_disk_without_decision( controller, 0 ) == OK );
      assert( fuse_host_new_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                           "api-outgoing.udi" ) == OK );
      assert( !remove( "api-outgoing.udi" ) );
      assert( drive->fdd->loaded && drive->fdd->disk.dirty && !drive->fdd->disk.filename );
      assert( !utils_write_file( "api-bad.udi", (const unsigned char *)"UDI!", 4 ) );
      assert( insert_disk_awaiting_eject( controller, "api-bad.udi", &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request,
                                                  FUSE_MEDIA_DECISION_DISCARD,
                                                  NULL ) ==
              FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( !drive->fdd->loaded && !remove( "api-bad.udi" ) );
    }
    if( has_second ) {
      assert( fuse_host_copy_disk_drive_info( controller, 1, &info ) == OK );
      assert( info->loaded && !strcmp( info->filename, "api-input.udi" ) );
      fuse_disk_drive_info_free( info );
      assert( eject_disk_without_decision( controller, 1 ) == OK );
    }
  }
  assert( !remove( "api-input.udi" ) && !remove( "api-copy.udi" ) );
  assert( disk_available_calls );
  drive->is_available = disk_available;
}
