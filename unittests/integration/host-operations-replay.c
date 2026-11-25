/* host-operations-replay.c: host operation integration tests: replay
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
write_snapshot_joystick_fixture( void )
{
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_SINCLAIR_1;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x8123;
  assert( !snapshot_write( "joystick.szx" ) );
}

extern int null_ui_confirm_joystick_calls;

void
check_snapshot_joystick_decisions( void )
{
  fuse_open_request request, next;
  fuse_media_decision_id other;
  const fuse_host_joystick keyboard = FUSE_HOST_JOYSTICK_KEYBOARD;
  const fuse_host_joystick invalid = (fuse_host_joystick)99;
  int answered = 0;

  write_snapshot_joystick_fixture();
  settings_current.joystick_1_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  settings_current.joy_kempston = 0;
  z80.pc.w = 0x9000;
  assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
  assert( fuse_host_open_file( "joystick.szx",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "joystick.szx" ) );
  assert( fuse_host_answer_snapshot_joystick( request.id, &invalid, &next ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT && !next.id && !next.question );
  do {
    fuse_host_operation_result result;
    assert( answered < 2 );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && z80.pc.w == 0x9000 );
    assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_host_request_tape_eject( &other ) ==
            FUSE_HOST_OPERATION_BUSY && !other );
    assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR ); /* Controller unavailable on 48K. */
    assert( settings_current.joystick_keyboard_output == JOYSTICK_TYPE_NONE );
    assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
    assert( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_KEMPSTON ||
            request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 );
    assert( request.detail.joystick.inputs ==
            ( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 ?
              LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 :
              LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_1 ) );
    result = fuse_host_answer_snapshot_joystick( request.id,
                                                 request.detail.joystick.type ==
                                                 LIBSPECTRUM_JOYSTICK_SINCLAIR_1
    ? &keyboard : NULL, &next );
    assert( result == ( next.id ? FUSE_HOST_OPERATION_NEEDS_DECISION : OK ) );
    assert( request.id != next.id );
    assert( fuse_host_answer_snapshot_joystick( request.id, NULL, &request ) ==
            FUSE_HOST_OPERATION_STALE && !request.id && !request.question );
    request = next;
    answered++;
  } while( request.id );
  assert( request.question == FUSE_OPEN_NO_QUESTION );
  assert( answered == 2 && z80.pc.w == 0x8123 );
  assert( settings_current.joystick_1_output == JOYSTICK_TYPE_NONE );
  assert( settings_current.joystick_keyboard_output == JOYSTICK_TYPE_SINCLAIR_1 );
  assert( settings_current.joy_kempston );
  assert( keyboard_return_values[1] == 0xff );
  assert( !null_ui_confirm_joystick_calls );
}

void
write_joystick_rzx( const char *filename, int embedded )
{
  libspectrum_rzx *recording = libspectrum_rzx_alloc();
  libspectrum_byte *buffer = NULL;
  size_t length = 0;
  int i;
  write_snapshot_joystick_fixture();
  writebyte_internal( 0x8123, 0x76 ); /* HALT: no port input during replay. */
  z80.iff1 = z80.iff2 = 0;
  assert( !snapshot_write( "joystick.szx" ) );
  if( embedded ) {
    libspectrum_snap *snap = libspectrum_snap_alloc();
    assert( !snapshot_copy_to( snap ) );
    assert( !libspectrum_rzx_add_snap( recording, snap, 0 ) );
  }
  libspectrum_rzx_start_input( recording, 0 );
  for( i = 0; i < 7; i++ )
    assert( !libspectrum_rzx_store_frame( recording, 16, 0, NULL ) );
  assert( !libspectrum_rzx_write( &buffer, &length, recording,
                                  LIBSPECTRUM_ID_SNAPSHOT_SZX, NULL, 0,
                                  NULL ) );
  assert( !utils_write_file( filename, buffer, length ) );
  libspectrum_free( buffer );
  assert( !libspectrum_rzx_free( recording ) );
}

void
check_rzx_open( int embedded )
{
  fuse_open_request request;
  fuse_host_operation_result result;
  int i;

  write_joystick_rzx( "joystick.rzx", embedded );
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_open_file( "joystick.rzx",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  if( !embedded ) {
    fuse_media_decision_id id = request.id;
    assert( request.question == FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !rzx_playback );
    assert( fuse_host_answer_rzx_external_snapshot( id, NULL,
                                                    &request ) ==
            FUSE_HOST_OPERATION_CANCELLED );
    assert( !request.id && !rzx_playback );
    assert( fuse_host_answer_rzx_external_snapshot( id, "joystick.szx",
                                                    &request ) ==
            FUSE_HOST_OPERATION_STALE );
    assert( fuse_host_open_file( "joystick.rzx",
                                 &request ) ==
            FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( request.id != id && !remove( "joystick.rzx" ) );
    assert( fuse_host_answer_rzx_external_snapshot( request.id, "joystick.szx",
                                                    &request ) ==
            FUSE_HOST_OPERATION_NEEDS_DECISION );
  } else {
    assert( !remove( "joystick.rzx" ) );
  }
  assert( !remove( "joystick.szx" ) );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !rzx_playback &&
          z80.pc.w == 0x9000 );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
  result = fuse_host_answer_snapshot_joystick( request.id, NULL, &request );
  assert( result == OK && !request.id && rzx_playback && z80.pc.w == 0x8123 );
  for( i = 0; i < 16 && rzx_playback; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !rzx_playback && !null_ui_confirm_joystick_calls );
}

void
check_rzx_movie( void )
{
  fuse_open_request request;
  utils_file movie;
  char target[] = "capture";
  int i;

  write_joystick_rzx( "joystick.rzx", 1 );
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  assert( !utils_write_file( "capture.fmf", (const unsigned char *)"old", 3 ) );
  assert( fuse_host_record_movie_from_rzx( "joystick.rzx", target, &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  target[0] = 'x'; /* The future output path belongs to Fuse. */
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( !rzx_playback && !movie_is_active() &&
          fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( !utils_read_file( "capture.fmf", &movie ) && movie.length == 3 );
  utils_close_file( &movie );
  assert( fuse_host_answer_snapshot_joystick( request.id, NULL,
                                              &request ) == OK );
  assert( !request.id && rzx_playback && movie_is_active() );
  for( i = 0; i < 16 && rzx_playback; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !rzx_playback && !movie_is_active() );
  assert( !utils_read_file( "capture.fmf", &movie ) && movie.length > 16 );
  assert( !memcmp( movie.buffer, "FMF_V1", 6 ) );
  utils_close_file( &movie );
  assert( !remove( "capture.fmf" ) );
  assert( fuse_host_record_movie_from_rzx( "joystick.rzx",
                                           "missing-directory/out.fmf",
                                           &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_answer_snapshot_joystick( request.id, NULL, &request ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !request.id && rzx_playback && !movie_is_active() );
  assert( fuse_host_stop_rzx() == OK && !rzx_playback );
  write_joystick_rzx( "external.rzx", 0 );
  assert( fuse_host_record_movie_from_rzx( "external.rzx", "cancelled.fmf",
                                           &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT );
  assert( fuse_host_answer_rzx_external_snapshot( request.id, NULL,
                                                  &request ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( !compat_file_exists( "cancelled.fmf" ) && !movie_is_active() );
  assert( !remove( "external.rzx" ) && !remove( "joystick.rzx" ) &&
          !remove( "joystick.szx" ) );
}

void
check_rzx_recording_operations( void )
{
  fuse_open_request request;
  assert( fuse_host_insert_rzx_snapshot() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_start_rzx_recording( "host.rzx",
                                         2 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_start_rzx_recording( "host.rzx",
                                         1 ) == OK && rzx_recording );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( fuse_host_play_rzx( "host.rzx",
                              &request ) == FUSE_HOST_OPERATION_BUSY &&
          !request.id );
  assert( fuse_host_finalise_rzx( "host.rzx" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( fuse_host_finalise_rzx( "host.rzx" ) == OK );
  assert( fuse_host_play_rzx( "host.rzx",
                              &request ) == OK && !request.id && rzx_playback );
  assert( fuse_host_stop_rzx() == OK && !rzx_playback );
  assert( !remove( "host.rzx" ) );
  assert( fuse_host_start_rzx_recording( "missing-directory/output.rzx",
                                         1 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == FUSE_HOST_OPERATION_MEDIA_ERROR &&
          !rzx_recording );
}

void
check_rzx_rollback( void )
{
  fuse_open_request request;
  size_t *frames, count;
  const fuse_host_joystick slot = FUSE_HOST_JOYSTICK_2;

  settings_current.joystick_2_output = JOYSTICK_TYPE_SINCLAIR_1;
  z80.pc.w = 0x8123;
  assert( fuse_host_start_rzx_recording( "rollback.rzx", 1 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  z80.pc.w = 0x8234;
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  z80.pc.w = 0x8345;
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_copy_rzx_rollback_points( &frames, &count ) == OK );
  assert( count == 3 && frames[0] == 0 && frames[1] == 1 && frames[2] == 2 );
  assert( fuse_host_rollback_rzx_to( count, &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT && !request.id );
  assert( fuse_host_rollback_rzx( &request ) == OK && z80.pc.w == 0x8345 );
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  assert( fuse_host_rollback_rzx_to( 1,
                                     &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( rzx_recording && z80.pc.w == 0x8345 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_snapshot_joystick( request.id, &slot,
                                              &request ) == OK );
  assert( !request.id && z80.pc.w == 0x8234 && rzx_recording );
  assert( frames[2] == 2 ); /* Independent of the truncated recording. */
  libspectrum_free( frames );
  assert( fuse_host_copy_rzx_rollback_points( &frames, &count ) == OK );
  assert( count == 2 && frames[1] == 1 );
  libspectrum_free( frames );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( !remove( "rollback.rzx" ) && !null_ui_confirm_joystick_calls );
}

void
check_rzx_recording_restore( void )
{
  fuse_open_request request;
  const fuse_host_joystick joystick = FUSE_HOST_JOYSTICK_2;
  fuse_media_decision_id id;
  write_snapshot_joystick_fixture();
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_record_rzx_from_snapshot( "joystick.szx", "restore.rzx", 1,
                                              &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK && !rzx_recording );
  assert( !remove( "joystick.szx" ) );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && z80.pc.w == 0x9000 );
  assert( fuse_host_stop_rzx() == FUSE_HOST_OPERATION_BUSY );
  id = request.id;
  assert( fuse_host_answer_snapshot_joystick( id, &joystick, &request ) == OK );
  assert( !request.id && rzx_recording && z80.pc.w == 0x8123 );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK );
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_continue_rzx_recording( "restore.rzx", &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK && request.id != id );
  assert( fuse_host_answer_snapshot_joystick( id, NULL,
                                              &(fuse_open_request){ 0 } ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !remove( "restore.rzx" ) );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION &&
          !rzx_recording );
  assert( fuse_host_answer_snapshot_joystick( request.id, &joystick,
                                              &request ) == OK );
  assert( rzx_recording && !request.id );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( !remove( "restore.rzx" ) && !null_ui_confirm_joystick_calls );
}
