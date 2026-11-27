/* lua-results.c: evidence finalization and structured result serialization.
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
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lua-host-internal.h"
#include "fuse-host-settings.h"
#include "artifacts.h"
#include "host-services.h"
#include "json.h"
#include "result_settings.h"
#include "state.h"

static int
finalize_artifacts( lua_host *h )
{
  int error;
  /* FAILED host mutations permit only shutdown, not renderer inspection. */
  error = automation_artifacts_write( h->output,
                                      h->screen &&
                                      ( !h->ready ||
                                        ( h->inspectable &&
                                          host_services_screen_available() ) ),
                                      h->audio );
  if( h->screen && h->ready &&
      ( !h->inspectable || !host_services_screen_available() ) ){
    automation_artifacts_screen_error( h->inspectable ?
                                       "no presented display frame in the current geometry"
    :
                                       "image encoding skipped after a failed host operation" );
    error = 1;
  }
  if( error && !h->failure ) h->failure = "artifact-error";
  return error;
}

static void
write_scenario( automation_json *json, lua_host *h )
{
  automation_json_object_begin( json, "scenario" );
  automation_json_string( json, "requested_machine",
                          h->machine[0] ? h->machine : "default" );
  automation_json_ulong( json, "maximum_frames", h->maximum_frames );
  automation_json_object_begin( json, "capture" );
  automation_json_boolean( json, "screen", h->screen );
  automation_json_boolean( json, "audio", h->audio );
  automation_json_end( json ); automation_json_end( json );
}

static void
write_execution( automation_json *json, lua_host *h,
                 const settings_info *settings, const char *message )
{
  automation_termination_type rzx_outcome;
  automation_json_object_begin( json, "execution" );
  automation_json_ulong( json, "frames_completed", h->frames );
  automation_json_string( json, "frame_accounting",
                          "completed-host-frame-turns" );
  automation_json_ulong( json, "tstate_requests", h->budgets );
  if( host_services_rzx_outcome( &rzx_outcome ) )
    automation_json_string( json, "rzx_termination",
                            lua_host_rzx_name( rzx_outcome ) );
  if( settings ) {
    automation_json_string( json, "actual_machine", settings->start_machine );
    automation_json_string( json, "cpu_mode",
                            settings->z80_is_cmos ? "cmos" : "nmos" );
  }
  automation_json_object_begin( json, "termination" );
  automation_json_string( json, "type",
                          h->failure ? h->failure : "script-end" );
  if( message ) automation_json_string( json, "message", message );
  automation_json_end( json ); automation_json_end( json );
}

static void
write_state( automation_json *json, lua_host *h )
{
  if( h->ready && h->inspectable ) {
    automation_state_write_json( json );
    automation_result_settings_write_json( json, h->audio, h->screen );
  }
}

static void
write_identity( automation_json *json, lua_host *h )
{
  size_t i;
  char crc[9];
  automation_json_object_begin( json, "identity" );
  automation_json_array_begin( json, "provided_roms" );
  for( i = 0; i < h->rom_count; i++ ) {
    automation_json_object_begin( json, NULL );
    automation_json_string( json, "name", h->roms[i].name );
    automation_json_string( json, "path", h->roms[i].path );
    automation_json_ulong( json, "size", h->roms[i].size );
    automation_json_boolean( json, "custom", h->roms[i].custom );
    snprintf( crc, sizeof( crc ), "%08lx", h->roms[i].crc );
    automation_json_string( json, "crc32", crc ); automation_json_end( json );
  }
  automation_json_end( json ); automation_json_end( json );
}

static void
write_script( automation_json *json, lua_host *h )
{
  size_t i;
  automation_json_object_begin( json, "script" );
  automation_json_string( json, "path", h->script );
  automation_json_string( json, "lua", LUA_RELEASE );
  automation_json_array_begin( json, "arguments" );
  for( i = 0; i < (size_t)h->argument_count; i++ )
    automation_json_string( json, NULL, h->arguments[i] );
  automation_json_end( json );
  automation_json_end( json );
}

static void
write_phases( automation_json *json, lua_host *h )
{
  size_t i;
  automation_json_array_begin( json, "phases" );
  for( i = 0; i < h->phase_count; i++ ) {
    phase *p = &h->phases[i]; automation_json_object_begin( json, NULL );
    automation_json_ulong( json, "frames_completed", p->frames );
    automation_json_ulong( json, "maximum_frames", p->maximum_frames );
    if( p->tstates ) automation_json_ulong( json, "requested_tstates",
                                            p->tstates );
    automation_json_object_begin( json, "termination" );
    automation_json_string( json, "type", p->termination );
    if( p->has_pc ) automation_json_ulong( json, "pc", p->pc );
    automation_json_end( json ); automation_json_end( json );
  }
  automation_json_end( json );
}

static void
write_diagnostics( automation_json *json, lua_host *h )
{
  size_t i;
  automation_json_array_begin( json, "diagnostics" );
  for( i = 0; i < h->diagnostic_count; i++ ) {
    automation_json_object_begin( json, NULL );
    automation_json_string( json, "severity",
                            h->diagnostics[i].level == FUSE_DIAGNOSTIC_ERROR ?
                            "error" : h->diagnostics[i].level ==
                            FUSE_DIAGNOSTIC_WARNING ? "warning" : "info" );
    automation_json_string( json, "message", h->diagnostics[i].text );
    automation_json_end( json );
  }
  automation_json_end( json );
  automation_json_ulong( json, "diagnostics_dropped", h->diagnostics_dropped );
}

static void
write_result_document( automation_json *json, lua_host *h,
                       const settings_info *settings, const char *message )
{
  automation_json_object_begin( json, NULL );
  automation_json_ulong( json, "schema", 1 );
  write_scenario( json, h );
  write_execution( json, h, settings, message );
  write_state( json, h );
  write_identity( json, h );
  write_script( json, h );
  write_phases( json, h );
  write_diagnostics( json, h );
  automation_json_object_begin( json, "artifacts" );
  automation_artifacts_write_json( json );
  automation_json_end( json );
  automation_json_end( json );
}

int
lua_host_write_result( lua_host *h, const char *message )
{
  char path[4096];
  FILE *file;
  automation_json json;
  settings_info *settings = NULL;
  int error, n;
  n = snprintf( path, sizeof( path ), "%s/result.json", h->output );
  if( n < 0 || (size_t)n >= sizeof( path ) ) return 1;
  error = finalize_artifacts( h );
  file = fopen( path, "wb" );
  if( !file ) {
    fprintf( stderr, "cannot write %s: %s\n", path, strerror( errno ) );
    return 1;
  }
  if( h->ready && h->inspectable ) fuse_host_copy_settings( &settings );
  automation_json_init( &json, file );
  write_result_document( &json, h, settings, message );
  fputc( '\n', file );
  if( automation_json_error( &json ) ) error = 1;
  if( fclose( file ) ) error = 1;
  fuse_host_free_settings( settings );
  return error;
}
