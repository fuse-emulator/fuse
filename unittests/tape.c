/* tape.c: tape unit tests
   Copyright (c) 1999-2017 Philip Kendall, Darren Salt, Witold Filipczyk
   Copyright (c) 2015-2018 UB880D
   Copyright (c) 2016-2021 Fredrick Meunier
   Copyright (c) 2026 Alberto Garcia

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include "config.h"

#include <stdio.h>

#include "libspectrum.h"

#include "tape.h"
#include "tape_internals.h"

typedef struct tape_test_fixture {
  libspectrum_tape *saved_tape;
  libspectrum_tape *test_tape;
  libspectrum_tape_block *rom;
  libspectrum_tape_block *following;
  libspectrum_byte *data;
  int saved_microphone;
} tape_test_fixture;

static int
tape_test_create_rom_pause_tape( tape_test_fixture *fixture )
{
  fixture->test_tape = libspectrum_tape_alloc();
  if( !fixture->test_tape ) return 1;

  fixture->rom =
    libspectrum_tape_block_alloc( LIBSPECTRUM_TAPE_BLOCK_ROM );
  fixture->following =
    libspectrum_tape_block_alloc( LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  fixture->data = libspectrum_new( libspectrum_byte, 2 );
  if( !fixture->rom || !fixture->following || !fixture->data ) return 1;

  fixture->data[0] = 0x80;
  fixture->data[1] = 0x80;
  libspectrum_tape_block_set_data_length( fixture->rom, 2 );
  libspectrum_tape_block_set_data( fixture->rom, fixture->data );
  fixture->data = NULL;
  libspectrum_tape_block_set_pause_tstates( fixture->rom, 3500000 );
  libspectrum_tape_block_set_pause_tstates( fixture->following, 1 );

  if( libspectrum_tape_append_block( fixture->test_tape, fixture->rom ) )
    return 1;
  fixture->rom = NULL;
  if( libspectrum_tape_append_block( fixture->test_tape,
                                     fixture->following ) )
    return 1;
  fixture->following = NULL;
  return 0;
}

static int
check_trap_pause_position( void )
{
  libspectrum_tape_signal_level level;
  int position;

  return tape_trap_finish_rom_block() ||
         libspectrum_tape_state( tape ) != LIBSPECTRUM_TAPE_STATE_PAUSE ||
         libspectrum_tape_position( &position, tape ) || position != 0 ||
         libspectrum_tape_signal_level_get( &level, tape ) ||
         level != LIBSPECTRUM_TAPE_SIGNAL_LOW || tape_microphone != 1;
}

static int
check_pause_playback( void )
{
  libspectrum_tape_edge edge;
  int position;

  /* Previewing must not consume the pause: normal playback returns the same
     high pause interval and only then selects the following block. */
  return libspectrum_tape_get_next_edge( &edge, tape ) ||
         edge.tstates != 3500000 ||
         edge.level != LIBSPECTRUM_TAPE_SIGNAL_HIGH ||
         !( edge.flags & LIBSPECTRUM_TAPE_FLAGS_BLOCK ) ||
         libspectrum_tape_position( &position, tape ) || position != 1;
}

static void
tape_test_cleanup( tape_test_fixture *fixture )
{
  tape = fixture->saved_tape;
  tape_microphone = fixture->saved_microphone;
  if( fixture->data ) libspectrum_free( fixture->data );
  if( fixture->rom ) libspectrum_tape_block_free( fixture->rom );
  if( fixture->following )
    libspectrum_tape_block_free( fixture->following );
  libspectrum_tape_free( fixture->test_tape );
}

int
tape_unittest( void )
{
  tape_test_fixture fixture = { 0 };
  int error;

  fixture.saved_tape = tape;
  fixture.saved_microphone = tape_microphone;
  error = tape_test_create_rom_pause_tape( &fixture );

  if( !error ) {
    tape = fixture.test_tape;
    tape_microphone = 0;
    error = check_trap_pause_position();
  }
  if( !error ) error = check_pause_playback();

  tape_test_cleanup( &fixture );
  if( error ) printf( "tape_unittest failed\n" );
  return error;
}
