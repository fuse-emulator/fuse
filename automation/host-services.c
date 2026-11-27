/* Headless application output, not emulator scheduling or phase policy.
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
#include "host-services.h"
#include "artifacts.h"

static int active, armed, screen_enabled, audio_enabled, screen_available;
static int audio_rate, audio_channels, motor_on, motor_seen;
static int rzx_playing, rzx_finished;
static automation_termination_type rzx_outcome;
static size_t pcm_bytes;
static const char *error;
static int resource_limit;

void
host_services_begin( int screen, int audio )
{
  active = 1;
  armed = 0;
  screen_enabled = screen;
  screen_available = 0;
  audio_enabled = audio;
  audio_rate = audio_channels = motor_on = motor_seen = 0;
  rzx_playing = rzx_finished = 0;
  pcm_bytes = 0;
  error = NULL;
  resource_limit = 0;
}

void
host_services_arm( void )
{
  armed = 1;
}

void
host_services_end( void )
{
  active = armed = screen_enabled = audio_enabled = 0;
}

int
host_services_active( void )
{
  return active;
}

int
host_services_screen_enabled( void )
{
  return active && screen_enabled;
}

int
host_services_screen_available( void )
{
  return screen_available;
}

void
host_services_screen_geometry( void )
{
  if( active ) screen_available = 0;
}

int
host_services_audio_enabled( void )
{
  return active && audio_enabled;
}

const char *
host_services_error( void )
{
  return error;
}

int
host_services_resource_limit( void )
{
  return resource_limit;
}

void
host_services_screen( const libspectrum_byte *pixels, size_t w, size_t h )
{
  if( active && armed && screen_enabled && pixels ) {
    automation_artifacts_capture_screen( pixels, w, h );
    screen_available = 1;
  }
}

void
host_services_audio_init( int rate, int channels )
{
  if( !host_services_audio_enabled() ) return;
  if( audio_rate && ( rate != audio_rate || channels != audio_channels ) ) {
    error = "audio format changed during capture";
    return; /* Keep the already captured stream's original format. */
  }
  audio_rate = rate;
  audio_channels = channels;
  automation_artifacts_audio_initialized( rate, channels );
}

void
host_services_audio( const libspectrum_signed_word *pcm, int count,
                     int rate, int channels )
{
  size_t bytes;
  if( !active || !armed || !audio_enabled || count <= 0 || error ) return;
  bytes = (size_t)count * sizeof( *pcm );
  if( bytes > 128u * 1024u * 1024u - pcm_bytes ) {
    error = "PCM capture exceeded 128 MiB";
    resource_limit = 1;
    return;
  }
  pcm_bytes += bytes;
  automation_artifacts_capture_pcm( pcm, count, rate, channels );
}

void
host_services_disk_changed( int on )
{
  if( !active ) return;
  motor_on = !!on;
  if( motor_on ) motor_seen = 1;
}

void
host_services_disk_reset( void )
{
  motor_seen = motor_on;
}

int
host_services_disk_on( void )
{
  return motor_on;
}

int
host_services_disk_observed( void )
{
  return motor_seen;
}

void
host_services_rzx_reset( void )
{
  rzx_playing = rzx_finished = 0;
}

void
host_services_rzx_started( void )
{
  if( !active ) return;
  rzx_playing = 1;
  rzx_finished = 0;
}

void
host_services_rzx_finished( automation_termination_type type )
{
  if( !active || rzx_finished ) return;
  if( type == AUTOMATION_TERMINATION_RZX_ABORTED && !rzx_playing ) return;
  rzx_outcome = type;
  rzx_playing = 0;
  rzx_finished = 1;
}

int
host_services_rzx_outcome( automation_termination_type *type )
{
  if( !rzx_finished ) return 0;
  *type = rzx_outcome;
  return 1;
}
