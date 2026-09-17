/* automation.c: development-only one-shot automation coordinator
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
#ifdef ENABLE_AUTOMATION

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "automation.h"
#include "json.h"
#include "fuse.h"
#include "machine.h"
#include "memory_pages.h"
#include "settings.h"
#include "utils.h"

typedef struct diagnostic { ui_error_level severity; char *message;} diagnostic;
static automation_scenario scenario;
static automation_result result;
static unsigned long first_frame;
static diagnostic *diagnostics;
static size_t diagnostics_count;
static uLong media_crc32;
static size_t media_size;
static int media_recorded;
static char *media_name;

static int
parse_count( const char *text, unsigned long *value, int allow_zero )
{
  char *end; errno = 0; *value = strtoul( text, &end, 0 );
  return errno || !text[0] || *end || ( !allow_zero && !*value );
}

static int
set_address( const char *text, automation_condition *condition )
{
  unsigned long value;
  if( parse_count( text, &value, 1 ) || value > 0xffff ) {
    fprintf( stderr, "invalid automation PC address: %s\n", text ); return 1;
  }
  condition->present = 1; condition->address = value; return 0;
}

int
automation_options_present( int argc, char **argv )
{
  for( int i = 1; i < argc; i++ ) {
    if( !strncmp( argv[i], "--automation-", 13 ) ) return 1;
  }

  return 0;
}

int
automation_set_output_directory( const char *directory )
{
  char *copy = strdup( directory ); if( !copy ) return 1;
  free( scenario.output_directory ); scenario.output_directory = copy; return 0;
}

int
automation_set_frame_limit( const char *frames )
{
  if( parse_count( frames, &scenario.maximum_frames, 0 ) ) {
    fprintf( stderr, "invalid automation frame count: %s\n", frames ); return 1;
  }
  return 0;
}

int
automation_set_success_pc( const char *text )
{
  return set_address( text, &scenario.success );
}

int
automation_set_failure_pc( const char *text )
{
  return set_address( text, &scenario.failure );
}

int
automation_set_failure_pc_ignore( const char *text )
{
  unsigned long value;
  if( parse_count( text, &value, 1 ) ) {
    fprintf( stderr, "invalid automation ignore count: %s\n", text ); return 1;
  }
  scenario.failure.ignore = value; return 0;
}

int
automation_validate_scenario( void )
{
  int conditions = scenario.success.present || scenario.failure.present;
  if( !scenario.output_directory && !scenario.maximum_frames && !conditions &&
      !scenario.failure.ignore ) return 0;
  if( !scenario.output_directory || !scenario.maximum_frames ||
      ( conditions && !scenario.success.present ) ||
      ( scenario.failure.ignore && !scenario.failure.present ) ) {
    fprintf( stderr,
             "automation requires --automation-output, a frame limit, and a success PC for condition runs\n" );
    return 1;
  }
  return 0;
}

int
automation_active( void )
{
  return scenario.output_directory != NULL;
}

void
automation_arm( unsigned long frame_count )
{
  first_frame = frame_count; result.frames_completed = 0;
  result.termination =
    scenario.success.present ? AUTOMATION_TERMINATION_DEADLINE :
    AUTOMATION_TERMINATION_FRAMES;
}

int
automation_check_pc( libspectrum_word pc )
{
  if( !automation_active() || !scenario.success.present ) return 0;
  if( scenario.failure.present && pc == scenario.failure.address ) {
    if( scenario.failure.hits++ >= scenario.failure.ignore ) {
      result.termination = AUTOMATION_TERMINATION_FAILURE; result.pc = pc;
      fuse_exiting = 1; return 1;
    }
  }
  if( pc == scenario.success.address ) {
    result.termination = AUTOMATION_TERMINATION_SUCCESS; result.pc = pc;
    fuse_exiting = 1; return 1;
  }
  return 0;
}

int
automation_frame_limit_reached( unsigned long frame_count )
{
  result.frames_completed = frame_count - first_frame;
  return result.frames_completed >= scenario.maximum_frames;
}

int
automation_exit_status( void )
{
  return result.termination ==
         AUTOMATION_TERMINATION_DEADLINE ? 2 : result.termination ==
         AUTOMATION_TERMINATION_FAILURE ? 1 : 0;
}

static uLong
checksum( const unsigned char *data, size_t length )
{
  uLong value = crc32( 0, Z_NULL, 0 );
  while( length ) {
    uInt part = length > UINT_MAX ? UINT_MAX : (uInt)length;
    value = crc32( value, data, part );
    data += part; length -= part;
  }
  return value;
}

void
automation_record_media( const utils_file *file )
{
  const char *name;
  if( libspectrum_file_class( file ) != LIBSPECTRUM_CLASS_TAPE ) return;
  media_size = libspectrum_file_size( file );
  media_crc32 = checksum( libspectrum_file_data( file ), media_size );
  media_recorded = 1;
  name = libspectrum_file_name( file ); free( media_name );
  media_name = name ? strdup( name ) : NULL;
}

void
automation_diagnostic( ui_error_level severity, const char *message )
{
  diagnostic *p; if( !automation_active() ) return;
  p = realloc( diagnostics, ( diagnostics_count + 1 ) * sizeof( *p ) );
  if( !p ) return;
  diagnostics = p; diagnostics[diagnostics_count].severity = severity;
  diagnostics[diagnostics_count].message = strdup( message );
  if( diagnostics[diagnostics_count].message ) diagnostics_count++;
}

static const char *
termination_name( void )
{
  switch( result.termination ) {
  case AUTOMATION_TERMINATION_SUCCESS: return "success";
  case AUTOMATION_TERMINATION_FAILURE: return "failure";
  case AUTOMATION_TERMINATION_DEADLINE: return "deadline";
  case AUTOMATION_TERMINATION_ERROR: return "error";
  default: return "frames";
  }
}

static void
write_crc32( automation_json *json, const unsigned char *data, size_t length )
{
  char text[9];
  snprintf( text, sizeof( text ), "%08lx", checksum( data, length ) );
  automation_json_string( json, "crc32", text );
}

int
automation_write_result( void )
{
  char *path; FILE *file; automation_json json;
  if( mkdir( scenario.output_directory, 0777 ) && errno != EEXIST ) return 1;
  path = malloc( strlen( scenario.output_directory ) + 13 );
  if( !path ) return 1;
  sprintf( path, "%s/result.json", scenario.output_directory );
  file = fopen( path, "w" ); free( path ); if( !file ) return 1;
  automation_json_init( &json, file );
  automation_json_object_begin( &json, NULL );
  automation_json_ulong( &json, "schema", 1 );
  automation_json_object_begin( &json, "scenario" );
  automation_json_ulong( &json, "maximum_frames", scenario.maximum_frames );
  automation_json_string( &json, "requested_machine",
                          settings_current.start_machine );
  if( scenario.success.present ) automation_json_ulong( &json, "success_pc",
                                                        scenario.success.address );
  if( scenario.failure.present ) {
    automation_json_ulong( &json, "failure_pc", scenario.failure.address );
    automation_json_ulong( &json, "failure_pc_ignore",
                           scenario.failure.ignore );
  }
  automation_json_end( &json );
  automation_json_object_begin( &json, "execution" );
  automation_json_ulong( &json, "frames_completed", result.frames_completed );
  automation_json_string( &json, "actual_machine", machine_current->id );
  automation_json_object_begin( &json, "termination" );
  automation_json_string( &json, "type", termination_name() );
  if( result.termination == AUTOMATION_TERMINATION_SUCCESS ||
      result.termination ==
      AUTOMATION_TERMINATION_FAILURE ) automation_json_ulong( &json, "pc",
                                                              result.pc );
  automation_json_end( &json ); automation_json_end( &json );
  automation_json_object_begin( &json, "identity" );
  if( media_recorded ) {
    char crc[9]; snprintf( crc, sizeof( crc ), "%08lx", media_crc32 );
    automation_json_object_begin( &json, "tape" );
    if( media_name ) automation_json_string( &json, "path", media_name );
    automation_json_ulong( &json, "size", media_size );
    automation_json_string( &json, "crc32", crc ); automation_json_end( &json );
  }
  automation_json_array_begin( &json, "active_roms" );
  for( int page = 0; page < SPECTRUM_ROM_PAGES; page++ ) {
    memory_page *p = &memory_map_rom[page * MEMORY_PAGES_IN_16K];
    if( !p->page ) continue;
    automation_json_object_begin( &json, NULL );
    automation_json_ulong( &json, "page", page );
    automation_json_ulong( &json, "size", 0x4000 );
    write_crc32( &json, p->page, 0x4000 ); automation_json_end( &json );
  }
  automation_json_end( &json ); automation_json_end( &json );
  automation_json_object_begin( &json, "settings" );
  automation_json_boolean( &json, "autoload", settings_current.auto_load );
  automation_json_boolean( &json, "fastload", settings_current.fastload );
  automation_json_boolean( &json, "tape_traps", settings_current.tape_traps );
  automation_json_boolean( &json, "loader_acceleration",
                           settings_current.accelerate_loader );
  automation_json_string( &json, "phantom_typist_mode",
                          settings_current.phantom_typist_mode );
  automation_json_end( &json );
  automation_json_array_begin( &json, "diagnostics" );
  for( size_t i = 0; i < diagnostics_count; i++ ) {
    automation_json_object_begin( &json, NULL );
    automation_json_string( &json, "severity",
                            diagnostics[i].severity ==
                            UI_ERROR_ERROR ? "error" :
                            diagnostics[i].severity ==
                            UI_ERROR_WARNING ? "warning" : "info" );
    automation_json_string( &json, "message", diagnostics[i].message );
    automation_json_end( &json );
  }
  automation_json_end( &json );
  automation_json_object_begin( &json, "artifacts" );
  automation_json_end( &json ); automation_json_end( &json );
  fputc( '\n', file );
  int error = automation_json_error( &json );
  if( fclose( file ) ) error = 1;

  return error;
}

void
automation_end( void )
{
  for( size_t i = 0; i < diagnostics_count;
       i++ ) free( diagnostics[i].message );
  free( diagnostics ); free( scenario.output_directory ); free( media_name );
}

#endif
