/* host-operations-memory.c: host operation integration tests: memory
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
check_host_poke_finder( void )
{
  fuse_poke_match *matches;
  size_t count;

  assert( fuse_host_copy_poke_matches( &matches,
                                       &count ) == FUSE_HOST_OPERATION_STALE );
  assert( !matches && !count );
  assert( fuse_host_add_custom_pok_trainer( 9, 0,
                                            1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_add_custom_pok_trainer( 8, 0x3fff,
                                            1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_reset_poke_finder() == OK );
  assert( fuse_host_copy_poke_matches( &matches, &count ) == OK && count );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK );
  assert( fuse_host_filter_poke_finder( FUSE_POKE_EXACT,
                                        0 ) == FUSE_HOST_OPERATION_STALE );
  assert( matches[0].offset < 0x4000 );
  libspectrum_free( matches );
  assert( fuse_host_reset_poke_finder() == OK );
  assert( fuse_host_filter_poke_finder( FUSE_POKE_INCREMENTED, 0 ) == OK );
  assert( fuse_host_copy_poke_matches( &matches,
                                       &count ) == OK && !matches && !count );
}

void
check_host_binary( void )
{
  static const libspectrum_byte input[] = { 0x12, 0x34, 0, 0xff, 0x56 };
  libspectrum_byte copy[4];
  utils_file output;

  assert( !utils_write_file( "input.bin", input, sizeof( input ) ) );
  assert( fuse_host_copy_memory( 0xffff, 2,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_memory( 0x10000, 1,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_memory( 0, 0,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_load_binary( "input.bin", 0xfffd,
                                 4 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_binary( "output.bin", 0xffff,
                                 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !compat_file_exists( "output.bin" ) );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_load_binary( "input.bin", 0xfffc, sizeof( copy ) ) == OK );
  assert( fuse_host_load_binary( "input.bin", 0xfffa,
                                 sizeof( input ) + 1 ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_load_binary( "missing.bin", 0xfffc,
                                 sizeof( copy ) ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_memory( 0xfffc, sizeof( copy ), copy ) == OK );
  assert( !memcmp( copy, input, sizeof( copy ) ) );
  assert( !utils_write_file( "output.bin", input, sizeof( input ) ) );
  assert( fuse_host_save_binary( "output.bin", 0xfffc, sizeof( copy ) ) == OK );
  assert( fuse_host_save_binary( "missing-directory/output.bin", 0xfffc,
                                 sizeof( copy ) ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( !utils_read_file( "output.bin", &output ) );
  assert( output.length == sizeof( copy ) && !memcmp( output.buffer, input,
                                                      output.length ) );
  utils_close_file( &output );
  assert( !remove( "input.bin" ) && !remove( "output.bin" ) );
}

extern int null_ui_pokemem_selector_calls;

void
check_pok_open( void )
{
  static const unsigned char payload[] =
    "NFixed\nZ 8 32768 99 17\nNCustom\nZ 8 32769 256 23\nY\n";
  fuse_pok_trainers *before, *after;
  fuse_open_request request;

  writebyte_internal( 0x8000, 17 );
  writebyte_internal( 0x8001, 23 );
  assert( !utils_write_file( "trainers.pok", payload, sizeof( payload ) ) );
  assert( fuse_host_open_file( "trainers.pok",
                               &request ) == OK && !request.id );
  assert( !null_ui_pokemem_selector_calls );
  assert( fuse_host_copy_pok_trainers( &before ) == OK );
  assert( before->count == 2 && !strcmp( before->trainers[0].name, "Fixed" ) );
  assert( !before->trainers[0].active && !before->trainers[0].asks_value );
  assert( before->trainers[1].asks_value && !before->trainers[1].disabled );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 1, -1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( readbyte_internal( 0x8001 ) == 23 );
  assert( fuse_host_set_pok_trainer( before->generation, 0, 1, -1 ) == OK );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 1, 42 ) == OK );
  assert( readbyte_internal( 0x8000 ) == 99 &&
          readbyte_internal( 0x8001 ) == 42 );
  assert( fuse_host_copy_pok_trainers( &after ) == OK );
  assert( after->trainers[0].active && after->trainers[1].active );
  assert( !before->trainers[0].active );
  fuse_pok_trainers_free( after );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 0, -1 ) == OK );
  assert( readbyte_internal( 0x8001 ) == 23 );
  assert( fuse_host_open_file( "trainers.pok", &request ) == OK );
  assert( readbyte_internal( 0x8000 ) == 17 );
  assert( fuse_host_set_pok_trainer( before->generation, 0, 1, -1 ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !strcmp( before->trainers[0].name, "Fixed" ) );
  fuse_pok_trainers_free( before );
  assert( !remove( "trainers.pok" ) );
}
