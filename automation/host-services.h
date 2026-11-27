/* Host-owned headless output services.
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

#ifndef FUSE_HOST_SERVICES_H
#define FUSE_HOST_SERVICES_H
#include "libspectrum.h"
#include "automation.h"

/* Capture policy belongs to the session, not its command language. A zero
   byte limit permits an unbounded stream; format changes can be admitted for
   compatibility with existing one-shot capture. */
typedef struct host_services_policy {
  size_t pcm_limit_bytes;
  int reject_audio_format_changes;
} host_services_policy;

void host_services_begin( int screen, int audio,
                          const host_services_policy *policy );
void host_services_arm( void );
void host_services_end( void );
int host_services_active( void );
int host_services_screen_enabled( void );
int host_services_screen_available( void );
void host_services_screen_geometry( void );
int host_services_audio_enabled( void );
void host_services_screen( const libspectrum_byte *pixels, size_t w, size_t h );
void host_services_audio_init( int rate, int channels );
void host_services_audio( const libspectrum_signed_word *pcm, int count,
                          int rate, int channels );
const char *host_services_error( void );
int host_services_resource_limit( void );
void host_services_disk_changed( int on );
void host_services_disk_reset( void );
int host_services_disk_on( void );
int host_services_disk_observed( void );
void host_services_rzx_reset( void );
void host_services_rzx_started( void );
void host_services_rzx_finished( automation_termination_type type );
int host_services_rzx_outcome( automation_termination_type *type );
#endif
