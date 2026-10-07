/* sdl2wakeuptest.c: device-free SDL2 producer/callback synchronization test
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

/* No SDL dylib constructor or audio thread is needed: test the actual backend
   callback concurrently with its producer, using native wait/wake primitives. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#ifdef SDL2_TEST_THREADS
#include <pthread.h>
#endif
#include "machine.h"
#include "settings.h"
#include "sound/audio_progress.h"
#include "ui/ui.h"
#define CHECK( x ) do { if( !( x ) ) { \
                          fprintf( stderr, "sdl2wakeuptest:%d: %s\n", __LINE__, \
                                   #x ); exit( 1 ); \
                        } } while( 0 )
static SDL_AudioCallback callback;
static int wake_before_wait( audio_progress_deadline );
static SDL_AudioDeviceID
mock_open( const char *name, int capture, const SDL_AudioSpec *want,
           SDL_AudioSpec *have, int flags )
{
  (void)name; (void)capture; (void)flags;
  *have = *want; have->samples = 512; callback = want->callback;
  return 1;
}

static int
mock_wait( struct audio_progress *p, uint32_t generation,
           audio_progress_deadline end )
{
  if( wake_before_wait( end ) ) return 0;
  return audio_progress_wait( p, generation, end );
}

#define SDL_OpenAudioDevice mock_open
#define SDL_PauseAudioDevice( id, pause ) ( (void)( id ), (void)( pause ) )
#define SDL_CloseAudioDevice( id ) ( (void)( id ) )
#define SDL_LockAudioDevice( id ) ( (void)( id ) )
#define SDL_UnlockAudioDevice( id ) ( (void)( id ) )
#define SDL_WasInit( flags ) ( flags )
#define SDL_InitSubSystem( flags ) ( (void)( flags ), 0 )
#define SDL_QuitSubSystem( flags ) ( (void)( flags ) )
#define SDL_setenv( name, value, overwrite ) \
        ( (void)( name ), (void)( value ), (void)( overwrite ), 0 )
#define SDL_GetError() "mock SDL error"
#define audio_progress_wait mock_wait
#include "sound/sdl2sound.c"
#undef audio_progress_wait
static fuse_machine_info test_machine;
fuse_machine_info *machine_current = &test_machine;
settings_info settings_current;
libspectrum_dword
sound_get_effective_processor_speed( void )
{
  return 3500000;
}

int
sound_normal_producer_context( void )
{
  return 1;
}

int
ui_error( ui_error_level level, const char *message, ... )
{
  (void)level; (void)message; return 0;
}

static void
wake_init( void )
{
}

static void
wake_consume( unsigned int frames )
{
  Uint8 output[1024];
  CHECK( frames <= 512 ); callback( NULL, output, frames * 2 );
}

#ifdef SDL2_TEST_THREADS
#define AUDIO_WAKE_THREADS
#endif
#include "audio-wakeup-cases.h"
int
main( void )
{
  test_machine.timings.tstates_per_frame = 70000;
  wake_regressions();
#ifdef AUDIO_WAKE_THREADS
  wake_threaded();
#endif
  puts( "SDL2 native wakeup tests passed" );
  return 0;
}
