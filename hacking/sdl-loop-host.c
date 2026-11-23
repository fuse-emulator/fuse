/* sdl-loop-host.c: same-thread SDL event-loop embedding probe
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "fuse-host.h"

/* Read A's keyboard row into RAM, then toggle the border and MIC with a
   delay. The memory byte is observable without using engine internals. */
static const unsigned char probe_rom[ 0x4000 ] = {
  0x01, 0xfe, 0xfd,       /* LD BC,$fdfe */
  0xed, 0x78,             /* IN A,(C) */
  0x32, 0x00, 0x80,       /* LD ($8000),A */
  0x3a, 0x01, 0x80,       /* LD A,($8001) */
  0xee, 0x09,             /* XOR $09 */
  0x32, 0x01, 0x80,       /* LD ($8001),A */
  0xd3, 0xfe,             /* OUT ($fe),A */
  0x06, 0x80, 0x10, 0xfe, /* LD B,$80; DJNZ $ */
  0x18, 0xe8              /* JR $0000 */
};

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  (void)context;
  if( strcmp( name, "48.rom" ) ) return 1;
  rom->data = probe_rom;
  rom->length = sizeof( probe_rom );
  return 0;
}

static int
key_state( int pressed )
{
  unsigned char state;
  return fuse_host_copy_memory( 0x8000, 1, &state ) == FUSE_HOST_OPERATION_OK &&
         ( ( state & 1 ) == 0 ) == pressed;
}

static int
post( Uint32 type, SDL_Keycode key )
{
  SDL_Event event;
  SDL_zero( event );
  event.type = type;
  if( type == SDL_KEYDOWN || type == SDL_KEYUP ) event.key.keysym.sym = key;
  return SDL_PushEvent( &event ) == 1;
}

#define CHECK( expression ) \
        do { \
          if( !( expression ) ) { \
            fprintf( stderr, "SDL host: %s failed (line %d)\n", #expression, \
                     __LINE__ ); \
            goto cleanup; \
          } \
        } while( 0 )

int
main( int argc, char **argv )
{
  const fuse_host_resources resources = { NULL, acquire_rom };
  fuse_host_config *config = NULL;
  fuse_quit_request quit;
  fuse_media_decision_id insert;
  fuse_tape_info *tape = NULL;
  SDL_Event event;
  Uint32 answer;
  unsigned int frame = 0;
  int status = EXIT_FAILURE, ready = 0, quitting = 0;
  if( argc != 2 ) {
    fprintf( stderr, "Usage: %s INPUT.tap\n", argv[0] );
    return status;
  }

  if( SDL_Init( SDL_INIT_VIDEO ) ) {
    fprintf( stderr, "SDL host: %s\n", SDL_GetError() );
    return status;
  }
  answer = SDL_RegisterEvents( 1 );
  CHECK( answer != ( Uint32 ) - 1 );
  config = fuse_host_config_alloc();
  CHECK( config );
  CHECK( !fuse_host_config_set_machine( config, "48" ) );
  CHECK( !fuse_host_config_set_sound( config, 0 ) );
  CHECK( !fuse_host_config_set_tape_traps( config, 0 ) );
  CHECK( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  ready = 1;
  CHECK( fuse_host_insert_tape_file( argv[1], &insert ) ==
         FUSE_HOST_OPERATION_OK && !insert );
  CHECK( fuse_host_copy_tape_info( &tape ) == FUSE_HOST_OPERATION_OK );
  CHECK( tape->present && !tape->playing && !tape->modified &&
         tape->block_count > 0 );
  printf( "SDL host: loaded and inspected %zu tape blocks\n",
          tape->block_count );
  fuse_tape_info_free( tape );
  tape = NULL;
  CHECK( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_OK );

  for( ;; ) {
    while( SDL_PollEvent( &event ) ) {
      if( event.type == answer ) {
        CHECK( quitting );
        CHECK( fuse_host_answer_quit( quit.id, FUSE_MEDIA_DECISION_DISCARD,
                                      NULL, &quit ) ==
               FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN );
        status = EXIT_SUCCESS;
        goto cleanup;
      }
      if( event.type == SDL_QUIT && !quitting ) {
        CHECK( fuse_host_stop_tape_recording() == FUSE_HOST_OPERATION_OK );
        CHECK( fuse_host_request_quit( &quit ) ==
               FUSE_HOST_OPERATION_NEEDS_DECISION );
        CHECK( quit.id && quit.media == FUSE_QUIT_TAPE );
        CHECK( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
        puts(
          "SDL host: received dirty tape decision; parked before answering" );
        quitting = 1;
        CHECK( post( answer, SDLK_UNKNOWN ) );
        break;
      }
      if( ( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ) &&
          event.key.keysym.sym == SDLK_a ) {
        CHECK( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A,
                                             event.type == SDL_KEYDOWN ) ==
               FUSE_HOST_OPERATION_OK );
      }
    }
    if( quitting ) continue;
    CHECK( fuse_run_frame() == FUSE_RUN_COMPLETED );
    switch( ++frame ) {
    case 1:
      CHECK( key_state( 0 ) && post( SDL_KEYDOWN, SDLK_a ) );
      break;
    case 2:
      CHECK( key_state( 1 ) );
      CHECK( fuse_host_set_paused( 1 ) == FUSE_HOST_OPERATION_OK );
      CHECK( fuse_run_frame() == FUSE_RUN_PAUSED && key_state( 1 ) );
      CHECK( fuse_host_set_paused( 0 ) == FUSE_HOST_OPERATION_OK );
      CHECK( post( SDL_KEYUP, SDLK_a ) );
      break;
    case 3:
      CHECK( key_state( 0 ) && post( SDL_QUIT, SDLK_UNKNOWN ) );
      break;
    default:
      CHECK( 0 );
    }
  }

cleanup:
  fuse_tape_info_free( tape );
  if( ready ) fuse_shutdown();
  if( config ) fuse_host_config_free( config );
  SDL_Quit();
  if( status == EXIT_SUCCESS )
    puts(
      "SDL host: input observed, pause/resume verified, quit answered and shut down" );
  return status;
}
