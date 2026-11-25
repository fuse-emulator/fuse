/* host-operations-peripherals.c: host operation integration tests: peripherals
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
check_microdrive( void )
{
  fuse_media_decision_id id, old;
  fuse_microdrive_info *info, *saved;
  assert( fuse_host_new_microdrive( 8,
                                    &id ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_new_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 5, &id ) == OK && !id );
  assert( fuse_host_set_microdrive_write_protected( 2, 1 ) == OK );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->available && info->inserted && info->modified &&
          info->write_protected && !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_save_microdrive( 2, "incoming.mdr" ) == OK );
  assert( fuse_host_eject_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_insert_microdrive( 2, "missing.mdr", &id ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR && !id );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  old = id;
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_eject_microdrive( 5,
                                      &id ) == FUSE_HOST_OPERATION_BUSY &&
          !id );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_SAVE,
                                             NULL ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_CANCEL,
                                             NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION && id != old );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !remove( "incoming.mdr" ) );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_SAVE,
                                             "missing-directory/old.mdr" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->inserted && info->modified && !info->write_protected &&
          !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_save_microdrive( 5, "incoming.mdr" ) == OK );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.mdr" ) );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) == OK );
  assert( fuse_host_copy_microdrive_info( 2, &saved ) == OK );
  assert( saved->inserted && !saved->modified && !strcmp( saved->filename,
                                                          "incoming.mdr" ) );
  assert( fuse_host_set_microdrive_write_protected( 2, 1 ) == OK );
  assert( fuse_host_new_microdrive( 2,
                                    &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_SAVE,
                                             NULL ) == OK );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->inserted && info->modified && !info->write_protected &&
          !info->filename );
  assert( !strcmp( saved->filename, "incoming.mdr" ) );
  fuse_microdrive_info_free( saved );
  fuse_microdrive_info_free( info );
  assert( fuse_host_copy_microdrive_info( 5, &info ) == OK );
  assert( info->inserted && !info->modified && !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.mdr" ) );
  fuse_shutdown(); /* Releases the staged file without requiring a reply. */
  assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
}

void
check_host_quit( int shutdown_pending )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  fuse_quit_request request;
  fuse_media_decision_id id, tape_id;
  disk_drive_info_t *a = disk_drive_find( controller, 0 );
  disk_drive_info_t *b = disk_drive_find( controller, 1 );
  static const unsigned char tap[] = { 2, 0, 0xff, 0xff };

  assert( fuse_host_new_microdrive( 3, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 1, &id ) == OK && !id );
  assert( fuse_host_select_machine( "plus3" ) == OK );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( new_disk_without_decision( controller, 1 ) == OK );
  assert( !utils_write_file( "quit.tap", tap, sizeof( tap ) ) );
  assert( insert_tape_without_decision( "quit.tap" ) == OK );
  assert( !remove( "quit.tap" ) );
  tape_modified = 1;
  assert( fuse_host_request_tape_eject( &id ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_BUSY && !request.id );
  assert( fuse_host_answer_tape_eject( id, FUSE_MEDIA_DECISION_CANCEL, NULL ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_request_quit( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION &&
          request.media == FUSE_QUIT_TAPE );
  tape_id = request.id;
  if( shutdown_pending ) {
    fuse_shutdown();
    assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
    assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                  &request ) == FUSE_HOST_OPERATION_NOT_READY );
    return;
  }
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && fuse_run_tstates( 1 ) == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_tape_eject( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_SAVE, NULL, &request ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_FLOPPY && request.controller == controller && request.slot == 0 && !tape_present() );
  id = request.id;
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_CANCEL, NULL, &request ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_quit( id, FUSE_MEDIA_DECISION_DISCARD, NULL, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_FLOPPY && request.slot == 1 && !a->fdd->loaded && b->fdd->loaded );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_CANCEL, NULL, &request ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( !request.id && b->fdd->loaded && b->fdd->disk.dirty && fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION && request.slot == 1 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
    "missing-directory/out.udi", &request ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( b->fdd->loaded && b->fdd->disk.dirty );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION && request.slot == 1 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
                                 "quit.udi",
                                 &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_MICRODRIVE && request.slot == 1 &&
          !request.has_save_target && !b->fdd->loaded );
  assert( fuse_host_answer_microdrive_eject( request.id,
                                             FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) ==
          FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                 &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_MICRODRIVE && request.slot == 3 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_CANCEL, NULL,
                                 &request ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( fuse_host_request_quit( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION &&
          request.media == FUSE_QUIT_MICRODRIVE && request.slot == 3 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
                                 "quit.mdr",
                                 &request ) ==
          FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN );
  assert( !remove( "quit.mdr" ) );
  assert( !request.id && fuse_run_frame() == FUSE_RUN_EXITING );
  assert( !remove( "quit.udi" ) );
  assert( !fuse_emulation_unpause() );
}

void
check_machine_buttons( int keep_fixture )
{
  static const fuse_machine_button buttons[] = {
    FUSE_MACHINE_BUTTON_MULTIFACE_RED,
    FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC,
    FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP
  };
  int *const enabled[] = { &settings_current.multiface1,
                           &settings_current.disciple,
                           &settings_current.didaktik80 };
  size_t i;
  int available;

  assert( fuse_host_press_machine_button( 99 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  for( i = 0; i < sizeof( buttons ) / sizeof( buttons[0] ); i++ ) {
    libspectrum_dword before;
    libspectrum_word pc;
    assert( fuse_host_machine_button_available( buttons[i],
                                                &available ) == OK &&
            !available );
    assert( fuse_host_press_machine_button( buttons[i] ) ==
            FUSE_HOST_OPERATION_FAILED );
    *enabled[i] = 1;
    (void)periph_update();
    assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
    assert( fuse_host_machine_button_available( buttons[i],
                                                &available ) == OK &&
            available );
    if( i == 1 ) {
      /* Let the reset-time paging guard expire without running ROM code. */
      writebyte_internal( 0x8000, 0x76 );
      z80.pc.w = 0x8000;
      assert( fuse_run_tstates( 64 ) == FUSE_RUN_COMPLETED );
    }
    if( keep_fixture && i == 0 )
      assert( fuse_host_save_snapshot_file( "button-multiface.szx" ) == OK );
    z80.sp.w = 0x9000;
    before = tstates; pc = z80.pc.w;
    assert( fuse_host_set_paused( 1 ) == OK );
    assert( fuse_host_press_machine_button( buttons[i] ) == OK );
    assert( tstates == before && z80.pc.w == pc );
    if( i == 0 ) {
      assert( multiface_activated && !multiface_active );
      assert( fuse_host_machine_button_available( buttons[i],
                                                  &available ) == OK &&
              !available );
      assert( fuse_host_press_machine_button( buttons[i] ) ==
              FUSE_HOST_OPERATION_FAILED );
    } else if( i == 1 ) {
      assert( !disciple_active );
    } else {
      assert( didaktik80_snap );
    }
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_PAUSED && tstates == before );
    assert( fuse_host_set_paused( 0 ) == OK );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED && z80.pc.w == 0x66 );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
    if( i == 0 ) assert( multiface_active );
    if( i == 1 ) assert( disciple_active );
    if( i == 2 ) {
      assert( !didaktik80_snap && z80.pc.w == 0 ); /* Existing forced RST 00. */
    }
    *enabled[i] = 0;
    (void)periph_update();
    assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
  }
}

void
check_rs232( void )
{
  fuse_rs232_info info;
  assert( fuse_host_copy_rs232_info( FUSE_RS232_RX, &info ) == OK );
  assert( !info.connected );
  assert( fuse_host_connect_rs232( 0,
                                   "unused" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_connect_rs232( FUSE_RS232_TX,
                                   "" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
#ifdef WIN32
  assert( !info.available );
  assert( fuse_host_connect_rs232( FUSE_RS232_RX,
                                   "rx.dat" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
#else
  FILE *file;
  assert( info.available );
  file = fopen( "rx.dat", "wb" );
  assert( file && !fclose( file ) );
  file = fopen( "tx.dat", "wb" );
  assert( file && !fclose( file ) );

  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_connect_rs232( FUSE_RS232_RX, "rx.dat" ) == OK );
  assert( fuse_host_connect_rs232( FUSE_RS232_TX, "tx.dat" ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK );
  assert( fuse_host_copy_rs232_info( FUSE_RS232_RX,
                                     &info ) == OK && info.connected );
  assert( fuse_host_connect_rs232( FUSE_RS232_RX,
                                   "missing-rx.dat" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_rs232_info( FUSE_RS232_RX,
                                     &info ) == OK && !info.connected );
  assert( fuse_host_copy_rs232_info( FUSE_RS232_TX,
                                     &info ) == OK && info.connected );
  assert( fuse_host_disconnect_rs232( FUSE_RS232_RX ) == OK );

  assert( fuse_host_select_machine( "plus3" ) == OK );
  assert( fuse_host_copy_rs232_info( FUSE_RS232_TX, &info ) == OK );
  assert( !info.available && info.connected );
  assert( fuse_host_disconnect_rs232( FUSE_RS232_TX ) == OK );
  assert( fuse_host_copy_rs232_info( FUSE_RS232_TX,
                                     &info ) == OK && !info.connected );
  assert( fuse_host_connect_rs232( FUSE_RS232_TX,
                                   "tx.dat" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !remove( "rx.dat" ) && !remove( "tx.dat" ) );
#endif
}

void
check_cartridge_lifecycle( void )
{
  const fuse_cartridge_slot if2 = FUSE_CARTRIDGE_INTERFACE2;
  const fuse_cartridge_slot dock = FUSE_CARTRIDGE_TIMEX_DOCK;
  static const unsigned char if2_image[ 0x4000 ] = { 0x76 };
  static const unsigned char dock_image[ 9 + 0x2000 ] = { 255, 2, 0, 0, 0, 0, 0,
                                                          0, 0, 0x3c };
  fuse_cartridge_info *if2_copy, *dock_copy, *info;
  unsigned char value;

  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( !info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( fuse_host_insert_cartridge( dock,
                                      "dock.dck" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_eject_cartridge( 99 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !utils_write_file( "host.if2", if2_image, sizeof( if2_image ) ) );
  assert( fuse_host_insert_cartridge( if2, "host.if2" ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_copy_cartridge_info( if2, &if2_copy ) == OK );
  assert( if2_copy->available && if2_copy->loaded &&
          !strcmp( if2_copy->filename, "host.if2" ) );
  assert( fuse_host_insert_cartridge( if2,
                                      "missing.if2" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_eject_cartridge( if2 ) == OK );
  assert( fuse_host_copy_cartridge_info( if2, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( !remove( "host.if2" ) );

  assert( fuse_host_select_machine( "2068" ) == OK );
  assert( !utils_write_file( "host.dck", dock_image, sizeof( dock_image ) ) );
  assert( fuse_host_insert_cartridge( dock, "host.dck" ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x3c );
  assert( fuse_host_copy_cartridge_info( dock, &dock_copy ) == OK );
  assert( dock_copy->available && dock_copy->loaded &&
          !strcmp( dock_copy->filename, "host.dck" ) );
  assert( !utils_write_file( "bad.dck", dock_image, 9 ) );
  assert( fuse_host_insert_cartridge( dock,
                                      "bad.dck" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !remove( "bad.dck" ) );
  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( fuse_host_insert_cartridge( dock, "host.dck" ) == OK );
  assert( fuse_host_eject_cartridge( dock ) == OK );
  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( !remove( "host.dck" ) );
  fuse_shutdown();
  assert( !strcmp( if2_copy->filename,
                   "host.if2" ) && !strcmp( dock_copy->filename, "host.dck" ) );
  fuse_cartridge_info_free( if2_copy );
  fuse_cartridge_info_free( dock_copy );
}
