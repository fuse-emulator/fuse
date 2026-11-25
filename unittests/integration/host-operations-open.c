/* host-operations-open.c: host operation integration tests: open
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
check_file_open( const char *mode )
{
  const unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  FILE *file;
  int i, requests, error;
  int autoload = settings_current.auto_load && !provider.custom;
  int (*reset)( int, libspectrum_snap * ) = machine_current->reset;
  libspectrum_dword clock;

  assert( settings_current.auto_load == ( strcmp( mode, "open-noauto" ) != 0 ) );
  fuse_open_request request = { .id = 1, .question = FUSE_OPEN_SAVE_TAPE };
  assert( fuse_host_open_file( NULL,
                               &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !request.id && request.question == FUSE_OPEN_NO_QUESTION );
  assert( fuse_host_open_file( "",
                               &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  file = fopen( "open.tap", "wb" ); assert( file );
  assert( fwrite( bytes, 1, sizeof(bytes), file ) == sizeof(bytes) && !fclose(file) );
  if( !strcmp( mode, "open-reset-failure" ) ) {
    machine_current->reset = fail_autoload_reset;
    assert( fuse_host_open_file( "open.tap",
                                 &request ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR );
    machine_current->reset = reset;
    assert( tape_present() && !phantom_typist_is_active() );
    /* No execution/retry after destructive failure; caller will shut down. */
  } else if( !strcmp( mode, "open-snapshot" ) || !strcmp( mode, "open-fallback" ) ) {
    if( !strcmp( mode, "open-fallback" ) ) assert( fuse_host_select_machine( "128" ) == OK );
    settings_current.late_timings = 1; z80.pc.w = 0x8123;
    assert( !snapshot_write( "open.szx" ) );
    if( !strcmp( mode, "open-fallback" ) ) {
      assert( fuse_host_select_machine( "48" ) == OK );
      provider.fail_name = "128-0.rom";
    }
    settings_current.late_timings = 0; z80.pc.w = 0x9234;
    assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
    assert( fuse_host_open_file( "open.szx", &request ) == OK );
    assert( !request.id && request.question == FUSE_OPEN_NO_QUESTION );
    assert( z80.pc.w == 0x8123 && settings_current.late_timings == 1 );
    if( !strcmp( mode, "open-fallback" ) ) {
      assert( machine_current->machine == LIBSPECTRUM_MACHINE_48 && provider.rejected == 1 );
      assert( fuse_host_keyboard_event( INPUT_KEY_space, 0 ) == OK );
    }
    for( i = 0; i < KEYBOARD_HALFROWS; i++ ) assert( keyboard_return_values[i] == 0xff );
    assert( !remove( "open.szx" ) );
  } else if( !strcmp( mode, "open-rzx-stop" ) ) {
    assert( !rzx_start_recording( "open-recording.rzx", 0 ) && rzx_recording );
    assert( fuse_host_open_file( "missing-open.tap",
                                 &request ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( !rzx_recording ); /* Stop occurs before even failed identification. */
    assert( !remove( "open-recording.rzx" ) );
    assert( fuse_host_open_file( "open.tap", &request ) == OK ); /* Ordinary read error permits retry. */
  } else {
    assert( !fuse_emulation_pause() );
    for( i = 0; i < 2; i++ ) {
      if( i ) { assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK ); tape_modified = 1; }
      z80.pc.w = 0x8123; clock = tstates; requests = provider.acquired;
      if( !i ) {
        fuse_engine_begin_operation();
        error = utils_open_file( "open.tap", tape_can_autoload(), NULL );
        display_refresh_all();
        fuse_engine_end_operation();
        assert( !error );
      } else {
        static const unsigned char pok[] = "NTest\nM 8 32768 99 0\nY\n";
        libspectrum_tape_block *old = libspectrum_tape_current_block( tape );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( request.id && request.question == FUSE_OPEN_SAVE_TAPE );
        assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
        assert( fuse_host_answer_tape_eject( request.id,
                                             FUSE_MEDIA_DECISION_CANCEL,
                                             NULL ) ==
                FUSE_HOST_OPERATION_CANCELLED );
        assert( tape_modified && libspectrum_tape_current_block( tape ) == old );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( fuse_host_answer_tape_eject( request.id, FUSE_MEDIA_DECISION_SAVE,
                                             "missing-directory/out.pzx" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
        assert( tape_modified && libspectrum_tape_current_block( tape ) == old );
        assert( !rzx_start_recording( "open-recording.rzx", 0 ) && rzx_recording );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( !rzx_recording && provider.acquired == requests );
        assert( !remove( "open-recording.rzx" ) && !remove( "open.tap" ) );
        assert( !utils_write_file( "open.pok", pok, sizeof( pok ) ) );
        assert( fuse_host_answer_tape_eject( request.id,
                                             FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) == OK );
        assert( !pokemem_autoload_pokfile() );
        assert( !remove( "open.pok" ) );
        assert( !utils_write_file( "open.tap", bytes, sizeof( bytes ) ) );
      }
      assert( provider.acquired == requests + autoload );
      assert( phantom_typist_is_active() == autoload );
      assert( z80.pc.w == ( autoload ? 0 : 0x8123 ) && tstates == clock );
      assert( tape_present() && !tape_modified && !tape_is_playing() && fuse_emulation_paused == 1 );
    }
    assert( !fuse_emulation_unpause() );
  }
  assert( !remove( "open.tap" ) );
}
