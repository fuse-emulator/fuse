/* hostservices.c: shared headless service lifecycle and capture policy tests
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

#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "automation/artifacts.h"
#include "automation/host-services.h"

/* Observe delivery without requiring an emulator, filesystem or codec. The
   integration tests exercise the real writers and compare complete PNG/WAVs. */
static int init_calls, audio_calls, screen_calls, end_calls;
static int last_rate, last_channels;
static size_t sample_count;
static const host_services_policy bounded = { 8, 1 };
static const host_services_policy compatible = { 0, 0 };
static const libspectrum_signed_word samples[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

void
automation_artifacts_audio_initialized( int rate, int channels )
{
  init_calls++;
  last_rate = rate;
  last_channels = channels;
}

void
automation_artifacts_capture_pcm( const libspectrum_signed_word *pcm,
                                  int count, int rate, int channels )
{
  assert( pcm == samples );
  audio_calls++;
  sample_count += count;
  last_rate = rate;
  last_channels = channels;
}

void
automation_artifacts_capture_screen( const libspectrum_byte *pixels,
                                     size_t width, size_t height )
{
  assert( pixels && width == 2 && height == 1 );
  screen_calls++;
}

void
automation_artifacts_end( void )
{
  end_calls++;
}

static void
reset_sinks( void )
{
  assert( !host_services_active() );
  init_calls = audio_calls = screen_calls = end_calls = 0;
  last_rate = last_channels = 0;
  sample_count = 0;
}

static void
check_arming_and_release( void )
{
  const libspectrum_byte pixels[2] = { 0, 1 };
  reset_sinks();
  host_services_begin( 1, 1, &bounded );
  assert( host_services_active() );
  assert( host_services_screen_enabled() && host_services_audio_enabled() );
  host_services_audio_init( 44100, 1 );
  host_services_audio( samples, 4, 44100, 1 );
  host_services_screen( pixels, 2, 1 );
  assert( init_calls == 1 && audio_calls == 0 && screen_calls == 0 );
  assert( !host_services_screen_available() );
  host_services_arm();
  host_services_audio( samples, 4, 44100, 1 );
  host_services_screen( pixels, 2, 1 );
  assert( audio_calls == 1 && sample_count == 4 && screen_calls == 1 );
  assert( host_services_screen_available() );
  host_services_screen_geometry();
  assert( !host_services_screen_available() );
  host_services_end();
  host_services_end();
  assert( end_calls == 1 && !host_services_active() );
  host_services_audio( samples, 4, 44100, 1 );
  host_services_screen( pixels, 2, 1 );
  assert( audio_calls == 1 && screen_calls == 1 );
}

static void
check_bounded_capture( void )
{
  const char *first_error;
  reset_sinks();
  host_services_begin( 0, 1, &bounded );
  host_services_audio_init( 44100, 1 );
  host_services_arm();
  host_services_audio( samples, 4, 44100, 1 );
  assert( !host_services_error() );
  host_services_audio( samples, 1, 44100, 1 );
  assert( host_services_resource_limit() && host_services_error() );
  assert( audio_calls == 1 && sample_count == 4 );
  first_error = host_services_error();
  host_services_audio_init( 22050, 2 );
  assert( host_services_error() == first_error && init_calls == 1 );
  host_services_end();
}

static void
check_format_rejection( void )
{
  reset_sinks();
  host_services_begin( 0, 1, &bounded );
  assert( !host_services_error() && !host_services_resource_limit() );
  host_services_audio_init( 44100, 1 );
  host_services_arm();
  host_services_audio( samples, 2, 44100, 1 );
  host_services_audio_init( 22050, 2 );
  assert( host_services_error() && !host_services_resource_limit() );
  assert( init_calls == 1 && last_rate == 44100 && last_channels == 1 );
  host_services_audio( samples, 2, 22050, 2 );
  assert( audio_calls == 1 );
  host_services_end();
}

static void
check_compatible_capture( void )
{
  reset_sinks();
  host_services_begin( 0, 1, &compatible );
  host_services_audio_init( 44100, 1 );
  host_services_arm();
  host_services_audio( samples, 8, 44100, 1 );
  host_services_audio_init( 22050, 2 );
  host_services_audio( samples, 8, 22050, 2 );
  assert( !host_services_error() && !host_services_resource_limit() );
  assert( init_calls == 2 && audio_calls == 2 && sample_count == 16 );
  assert( last_rate == 22050 && last_channels == 2 );
  host_services_end();
}

static void
check_observations_and_no_capture( void )
{
  automation_termination_type outcome;
  reset_sinks();
  host_services_begin( 0, 0, &compatible );
  assert( host_services_active() ); /* Virtual time needs no capture. */
  host_services_disk_changed( 1 );
  host_services_disk_reset();
  assert( host_services_disk_on() && host_services_disk_observed() );
  host_services_disk_changed( 0 );
  assert( !host_services_disk_on() && host_services_disk_observed() );
  host_services_disk_reset();
  assert( !host_services_disk_observed() );
  host_services_rzx_started();
  host_services_rzx_finished( AUTOMATION_TERMINATION_RZX_END );
  host_services_rzx_finished( AUTOMATION_TERMINATION_RZX_ABORTED );
  assert( host_services_rzx_outcome( &outcome ) );
  assert( outcome == AUTOMATION_TERMINATION_RZX_END );
  host_services_rzx_reset();
  assert( !host_services_rzx_outcome( &outcome ) );
  host_services_arm();
  host_services_audio_init( 44100, 1 );
  host_services_audio( samples, 4, 44100, 1 );
  assert( !init_calls && !audio_calls );
  host_services_end();
}

int
main( void )
{
  check_arming_and_release();
  check_bounded_capture();
  check_format_rejection();
  check_compatible_capture();
  check_observations_and_no_capture();
  puts(
    "Shared services: arming, delivery, policy, observations and teardown passed" );
  return 0;
}
