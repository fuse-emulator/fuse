/* lua-resources.c: startup configuration, ROM provision and diagnostics.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lauxlib.h>

#include "lua-host-internal.h"
#include "fuse-host-settings.h"
#include "artifacts.h"
#include "host-services.h"

static void
diagnostic( void *opaque, fuse_diagnostic_level level, const char *text )
{
  lua_host *h = opaque;
  size_t n;
  char *copy;
  if( level == FUSE_DIAGNOSTIC_ERROR ) h->diagnostic_errors++;
  if( h->diagnostic_count == MAX_DIAGNOSTICS ) {
    h->diagnostics_dropped++; return;
  }
  n = strlen( text );
  if( n > 8192 ) n = 8192;
  copy = malloc( n + 1 );
  if( !copy ) {
    h->diagnostics_dropped++; return;
  }
  memcpy( copy, text, n ); copy[n] = 0;
  h->diagnostics[h->diagnostic_count++] = (diagnostic_entry){ level, copy };
}

static void
release_rom( void *bytes )
{
  free( bytes );
}

static int
acquire_rom( void *opaque, const char *name, fuse_host_rom *rom )
{
  lua_host *h = opaque;
  char path[4096];
  FILE *file;
  long length;
  unsigned char *bytes;
  size_t i;
  int custom = 0, n;
  for( i = 0; i < h->overrides_count; i++ ) {
    if( !strcmp( name, h->overrides[i].name ) ) {
      custom = 1; break;
    }
  }
  n = custom ? snprintf( path, sizeof( path ), "%s", h->overrides[i].path ) :
      snprintf( path, sizeof( path ), "%s/%s", h->rom_directory, name );
  if( n < 0 || (size_t)n >= sizeof( path ) ) return 1;
  file = fopen( path, "rb" );
  if( !file ) return 1;
  if( fseek( file, 0, SEEK_END ) || ( length = ftell( file ) ) <= 0 ||
      length > 1024 * 1024 || fseek( file, 0, SEEK_SET ) ) {
    fclose( file ); return 1;
  }
  bytes = malloc( (size_t)length );
  if( !bytes ) {
    fclose( file ); return 1;
  }
  rom->context = bytes; rom->release = release_rom;
  n = fread( bytes, 1, (size_t)length, file ) != (size_t)length;
  if( fclose( file ) ) n = 1;
  if( n ) return 1; /* Engine releases the allocation, including failures. */
  rom->data = bytes; rom->length = (size_t)length; rom->custom = custom;
  for( i = 0; i < h->rom_count;
       i++ ) if( !strcmp( name, h->roms[i].name ) ) break;
  if( i == MAX_ROMS || strlen( name ) >= sizeof( h->roms[i].name ) ) return 1;
  if( i == h->rom_count ) h->rom_count++;
  snprintf( h->roms[i].name, sizeof( h->roms[i].name ), "%s", name );
  snprintf( h->roms[i].path, sizeof( h->roms[i].path ), "%s", path );
  h->roms[i].size = (size_t)length;
  h->roms[i].custom = custom;
  h->roms[i].crc = automation_checksum( bytes, (size_t)length );
  return 0;
}

typedef struct startup_options {
  int load, traps, divmmc, interface1;
} startup_options;

static void
parse_capture( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  static const char *const capture_keys[] = { "screen", "audio", NULL };
  h->screen = h->default_screen; h->audio = h->default_audio;
  lua_getfield( L, 1, "capture" );
  if( !lua_isnil( L, -1 ) ) {
    luaL_checktype( L, -1, LUA_TTABLE );
    lua_host_keys( L, lua_gettop( L ), capture_keys );
    h->screen = lua_host_boolean_field( L, lua_gettop( L ), "screen",
                                        h->screen );
    h->audio = lua_host_boolean_field( L, lua_gettop( L ), "audio", h->audio );
  }
  lua_pop( L, 1 );
}

static void
parse_rom_overrides( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  lua_getfield( L, 1, "roms" );
  if( !lua_isnil( L, -1 ) ) {
    luaL_checktype( L, -1, LUA_TTABLE );
    lua_pushnil( L );
    while( lua_next( L, -2 ) ) {
      const char *name, *path;
      size_t a, b;
      luaL_checktype( L, -2, LUA_TSTRING );
      name = lua_tolstring( L, -2, &a ); path = luaL_checklstring( L, -1, &b );
      if( !a || !b || a >= 256 || b >= 4096 || strlen( name ) != a ||
          strlen( path ) != b || h->overrides_count == MAX_ROMS )
        lua_host_fail( L, "invalid ROM override" );
      memcpy( h->overrides[h->overrides_count].name, name, a + 1 );
      memcpy( h->overrides[h->overrides_count++].path, path, b + 1 );
      lua_pop( L, 1 );
    }
  }
  lua_pop( L, 1 );
}

static startup_options
parse_start_options( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  startup_options options;
  static const char *const allowed[] = { "machine", "rom_directory", "roms",
                                         "auto_load", "tape_traps", "divmmc",
                                         "interface1", "capture", NULL };
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_host_keys( L, 1, allowed );
  lua_host_string_field( L, 1, "machine", h->machine, sizeof( h->machine ) );
  lua_host_string_field( L, 1, "rom_directory", h->rom_directory,
                         sizeof( h->rom_directory ) );
  options.load = lua_host_boolean_field( L, 1, "auto_load", -1 );
  options.traps = lua_host_boolean_field( L, 1, "tape_traps", -1 );
  options.divmmc = lua_host_boolean_field( L, 1, "divmmc", -1 );
  options.interface1 = lua_host_boolean_field( L, 1, "interface1", -1 );
  parse_capture( L );
  parse_rom_overrides( L );
  return options;
}

static int
configure_host( fuse_host_config *config, lua_host *h, startup_options options )
{
  return ( h->machine[0] && fuse_host_config_set_machine( config,
                                                          h->machine ) ) ||
         ( options.load >= 0 && fuse_host_config_set_auto_load( config,
                                                                options.load ) )
         ||
         ( options.traps >= 0 && fuse_host_config_set_tape_traps( config,
                                                                  options.traps ) )
         ||
         ( options.divmmc >= 0 && fuse_host_config_set_divmmc( config,
                                                               options.divmmc ) )
         ||
         ( options.interface1 >= 0 && fuse_host_config_set_interface1( config,
                                                                       options.
                                                                       interface1 ) )
         ||
         fuse_host_config_set_sound( config, h->audio ) ||
         fuse_host_config_set_diagnostics( config, diagnostic, h );
}

static void
resolve_startup_defaults( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  settings_info *current;
  if( h->machine[0] ) return;
  lua_host_operation( L, "read startup settings",
                      fuse_host_copy_settings( &current ) );
  snprintf( h->machine, sizeof( h->machine ), "%s", current->start_machine );
  fuse_host_free_settings( current );
}

int
lua_host_start( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  fuse_host_config *config;
  fuse_host_resources resources = { h, acquire_rom };
  fuse_init_result result;
  startup_options options;
  if( h->closed ) return lua_host_fail( L, "emulator host is shutting down" );
  if( h->attempted ) return lua_host_fail( L,
                                           "only one fuse.start attempt is allowed per process" );
  options = parse_start_options( L );
  config = fuse_host_config_alloc();
  if( !config ) return lua_host_fail( L, "cannot allocate host configuration" );
  if( configure_host( config, h, options ) ) {
    fuse_host_config_free( config );
    return lua_host_fail( L, "invalid host configuration" );
  }
  h->attempted = 1;
  host_services_begin( h->screen, h->audio );
  result = fuse_host_prepare( config, &resources );
  fuse_host_config_free( config );
  if( result != FUSE_INIT_READY ) {
    h->failure = "engine-error";
    return luaL_error( L, "fuse.start failed (initialization status %d)",
                       (int)result );
  }
  h->ready = h->inspectable = 1;
  resolve_startup_defaults( L );
  return 0;
}
