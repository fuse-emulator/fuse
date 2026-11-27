/* lua-options.c: Lua application command-line options.
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
#include <stdlib.h>
#include <string.h>

#include "lua-host-internal.h"

static int
parse_limit( const char *text, uint64_t *out )
{
  char *end;
  unsigned long long n;
  if( !*text || *text == '-' ) return 1;
  errno = 0; n = strtoull( text, &end, 0 );
  if( errno || *end || !n ) return 1;
  *out = n; return 0;
}

void
lua_host_usage( void )
{
  puts( "Usage: fuse-lua --script FILE --output DIR [--rom-directory DIR]\n"
        "                [--max-total-frames N] [--instruction-limit N]\n"
        "                [--capture-screen] [--capture-audio] [-- script arguments]\n"
        "Use --script - to read Lua source from stdin.\n"
        "Without --script, the existing Fuse command-line interface is accepted." );
}

typedef enum {
  OPTION_SCRIPT, OPTION_OUTPUT, OPTION_ROM_DIRECTORY,
  OPTION_FRAMES, OPTION_INSTRUCTIONS, OPTION_SCREEN, OPTION_AUDIO
} option_kind;

typedef struct host_option {
  const char *name;
  option_kind kind;
} host_option;

static const host_option options[] = {
  { "--script", OPTION_SCRIPT },
  { "--output", OPTION_OUTPUT },
  { "--automation-output", OPTION_OUTPUT },
  { "--rom-directory", OPTION_ROM_DIRECTORY },
  { "--max-total-frames", OPTION_FRAMES },
  { "--instruction-limit", OPTION_INSTRUCTIONS },
  { "--capture-screen", OPTION_SCREEN },
  { "--automation-capture-screen", OPTION_SCREEN },
  { "--capture-audio", OPTION_AUDIO },
  { "--automation-capture-audio", OPTION_AUDIO },
  { NULL, 0 }
};

static const host_option *
find_option( const char *name )
{
  size_t i;
  for( i = 0; options[i].name; i++ ) {
    if( !strcmp( name, options[i].name ) ) return &options[i];
  }
  return NULL;
}

static int
set_limit( lua_host *h, option_kind kind, const char *value )
{
  uint64_t limit;
  if( parse_limit( value, &limit ) || limit > 0x7fffffffUL ) return 1;
  if( kind == OPTION_FRAMES ) h->maximum_frames = (unsigned long)limit;
  else h->instructions = limit;
  return 0;
}

static int
set_option( lua_host *h, option_kind kind, const char *value )
{
  switch( kind ) {
  case OPTION_SCRIPT: h->script = value; return 0;
  case OPTION_OUTPUT: h->output = value; return 0;
  case OPTION_ROM_DIRECTORY:
    if( strlen( value ) >= sizeof( h->rom_directory ) ) return 1;
    strcpy( h->rom_directory, value ); return 0;
  case OPTION_FRAMES:
  case OPTION_INSTRUCTIONS: return set_limit( h, kind, value );
  case OPTION_SCREEN: h->default_screen = 1; return 0;
  case OPTION_AUDIO: h->default_audio = 1; return 0;
  }
  return 1;
}

int
lua_host_script_mode( int argc, char **argv )
{
  int i;
  for( i = 1; i < argc; i++ ) {
    if( !strcmp( argv[i], "--" ) ) break;
    if( !strcmp( argv[i], "--script" ) ) return 1;
  }
  return 0;
}

int
lua_host_parse_options( lua_host *h, int argc, char **argv )
{
  int i, args = argc;
  h->maximum_frames = 1000000;
  h->instructions = 10000000;
  strcpy( h->rom_directory, "roms" );
  for( i = 1; i < argc; i++ ) {
    const host_option *option;
    const char *value = NULL;
    if( !strcmp( argv[i], "--" ) ) {
      args = i + 1; break;
    }
    option = find_option( argv[i] );
    if( !option ) {
      fprintf( stderr, "unknown script option: %s\n", argv[i] );
      return 1;
    }
    if( option->kind != OPTION_SCREEN && option->kind != OPTION_AUDIO ) {
      if( i + 1 == argc ) {
        lua_host_usage(); return 1;
      }
      value = argv[++i];
    }
    if( set_option( h, option->kind, value ) ) return 1;
  }
  if( !h->script ) {
    fprintf( stderr, "script mode requires --script FILE\n" );
    return 1;
  }
  if( !h->output ) {
    fprintf( stderr, "script mode requires --output DIR\n" );
    return 1;
  }
  h->screen = h->default_screen;
  h->audio = h->default_audio;
  h->argument_count = argc - args;
  h->arguments = argv + args;
  return 0;
}
