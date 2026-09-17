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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "automation.h"
#include "utils.h"

typedef struct diagnostic {
  ui_error_level severity;
  char *message;
} diagnostic;

static automation_scenario scenario;
static automation_result result;
static unsigned long first_frame;
static diagnostic *diagnostics;
static size_t diagnostics_count;

static int
parse_count( const char *text, unsigned long *value )
{
  char *end;
  errno = 0;
  *value = strtoul( text, &end, 10 );
  return errno || !text[0] || *end || !*value;
}

int
automation_options_present( int argc, char **argv )
{
  for( int i = 1; i < argc; i++ )
    if( !strncmp( argv[i], "--automation-", 13 ) ) return 1;

  return 0;
}

int
automation_set_output_directory( const char *directory )
{
  char *copy = strdup( directory );
  if( !copy ) return 1;

  free( scenario.output_directory );
  scenario.output_directory = copy;
  return 0;
}

int
automation_set_frame_limit( const char *frames )
{
  if( parse_count( frames, &scenario.maximum_frames ) ) {
    fprintf( stderr, "invalid automation frame count: %s\n", frames );
    return 1;
  }
  return 0;
}

int
automation_validate_scenario( void )
{
  if( !!scenario.output_directory != !!scenario.maximum_frames ) {
    fprintf( stderr,
             "--automation-output and --automation-frames must be used together\n" );
    return 1;
  }
  return 0;
}

int
automation_active( void )
{
  return scenario.output_directory != NULL;
}

unsigned long
automation_maximum_frames( void )
{
  return scenario.maximum_frames;
}

void
automation_arm( unsigned long frame_count )
{
  first_frame = frame_count;
  result.frames_completed = 0;
  result.termination = AUTOMATION_TERMINATION_FRAMES;
}

int
automation_frame_limit_reached( unsigned long frame_count )
{
  result.frames_completed = frame_count - first_frame;
  return result.frames_completed >= scenario.maximum_frames;
}

void
automation_diagnostic( ui_error_level severity, const char *message )
{
  diagnostic *new_diagnostics;
  if( !automation_active() ) return;
  new_diagnostics = realloc( diagnostics,
                             ( diagnostics_count + 1 ) *
                             sizeof( *diagnostics ) );
  if( !new_diagnostics ) return;
  diagnostics = new_diagnostics;
  diagnostics[diagnostics_count].severity = severity;
  diagnostics[diagnostics_count].message = strdup( message );
  if( diagnostics[diagnostics_count].message ) diagnostics_count++;
}

static void
write_json_string( FILE *file, const char *text )
{
  fputc( '"', file );
  for( const unsigned char *p = (const unsigned char *)text; *p; p++ ) {
    switch( *p ) {
    case '"': fputs( "\\\"", file ); break;
    case '\\': fputs( "\\\\", file ); break;
    case '\n': fputs( "\\n", file ); break;
    case '\r': fputs( "\\r", file ); break;
    case '\t': fputs( "\\t", file ); break;
    default:
      if( *p < 0x20 ) fprintf( file, "\\u%04x", *p );
      else fputc( *p, file );
    }
  }
  fputc( '"', file );
}

int
automation_write_result( void )
{
  char *path;
  FILE *file;

  if( mkdir( scenario.output_directory, 0777 ) && errno != EEXIST ) return 1;
  path = malloc( strlen( scenario.output_directory ) + 13 );
  if( !path ) return 1;
  sprintf( path, "%s/result.json", scenario.output_directory );
  file = fopen( path, "w" );
  free( path );
  if( !file ) return 1;

  fprintf( file,
           "{\"schema\":1,\"scenario\":{\"maximum_frames\":%lu},"
           "\"execution\":{\"frames_completed\":%lu,"
           "\"termination\":{\"type\":\"frames\"}},\"diagnostics\":[",
           scenario.maximum_frames, result.frames_completed );
  for( size_t i = 0; i < diagnostics_count; i++ ) {
    if( i ) fputc( ',', file );
    fputs( "{\"severity\":", file );
    write_json_string( file,
                       diagnostics[i].severity == UI_ERROR_ERROR ? "error" :
                       diagnostics[i].severity ==
                       UI_ERROR_WARNING ? "warning" : "info" );
    fputs( ",\"message\":", file );
    write_json_string( file, diagnostics[i].message );
    fputc( '}', file );
  }
  fputs( "]}\n", file );
  return fclose( file ) != 0;
}

void
automation_end( void )
{
  for( size_t i = 0; i < diagnostics_count; i++ )
    free( diagnostics[i].message );
  free( diagnostics );
  free( scenario.output_directory );
}

#endif
