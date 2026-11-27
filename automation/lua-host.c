/* fuse-lua: a host-owned Lua automation application.
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
#include "host-services.h"
#include "fuse-legacy.h" /* Transitional command-line compatibility entry. */

static void *
lua_allocate( void *opaque, void *pointer, size_t old, size_t size )
{
  lua_host *h = opaque;
  void *p;
  if( !pointer ) old = 0; /* Lua's osize may be a type tag on allocation. */
  if( !size ) {
    free( pointer ); h->lua_bytes -= old; return NULL;
  }
  if( size > LUA_HEAP_LIMIT ||
      h->lua_bytes - old > LUA_HEAP_LIMIT - size ) return NULL;
  p = realloc( pointer, size );
  if( p ) h->lua_bytes = h->lua_bytes - old + size;
  return p;
}

static void
instruction_hook( lua_State *L, lua_Debug *debug )
{
  lua_host *h = *(lua_host **)lua_getextraspace( L );
  (void)debug;
  if( h->instructions <= (unsigned int)h->hook_interval ) {
    h->instructions = 0;
    h->failure = "resource-limit";
    luaL_error( L, "Lua instruction budget exhausted" );
  }
  h->instructions -= h->hook_interval;
  h->hook_interval = h->instructions < 1000 ? (int)h->instructions : 1000;
  lua_sethook( L, instruction_hook, LUA_MASKCOUNT, h->hook_interval );
}

static int
traceback( lua_State *L )
{
  const char *message = lua_tostring( L, 1 );
  luaL_traceback( L, L, message ? message : "non-string Lua error", 1 );
  return 1;
}

static int
execute_script( lua_State *L )
{
  lua_host *h = *(lua_host **)lua_getextraspace( L );
  int status = luaL_loadfile( L, !strcmp( h->script, "-" ) ? NULL : h->script );
  if( status != LUA_OK ) {
    if( status == LUA_ERRMEM ) h->failure = "resource-limit";
    return lua_error( L );
  }
  lua_call( L, 0, 0 );
  if( !h->ready ) return luaL_error( L, "script returned without fuse.start" );
  return 0;
}

static lua_State *
create_lua_state( lua_host *h )
{
  lua_State *L;
#if LUA_VERSION_NUM >= 505
  L = lua_newstate( lua_allocate, h, 0 );
#else
  L = lua_newstate( lua_allocate, h );
#endif
  if( !L ) return NULL;
  *(lua_host **)lua_getextraspace( L ) = h;
  lua_host_libraries( L, h, h->argument_count, h->arguments );
  h->hook_interval = h->instructions < 1000 ? (int)h->instructions : 1000;
  lua_sethook( L, instruction_hook, LUA_MASKCOUNT, h->hook_interval );
  return L;
}

static const char *
execute_lua( lua_State *L, lua_host *h )
{
  int status;
  const char *message;
  lua_pushcfunction( L, traceback );
  lua_pushcfunction( L, execute_script );
  status = lua_pcall( L, 0, 0, 1 );
  if( status == LUA_OK ) return NULL;
  message = lua_tostring( L, -1 );
  if( !message ) message = "Lua execution failed";
  if( !h->failure ) h->failure = status ==
                                 LUA_ERRMEM ? "resource-limit" : "script-error";
  fprintf( stderr, "%s\n", message );
  return message;
}

static const char *
capture_failure( lua_host *h, const char *message )
{
  if( !host_services_error() || h->failure ) return message;
  h->failure =
    host_services_resource_limit() ? "resource-limit" : "capture-error";
  message = host_services_error();
  fprintf( stderr, "%s\n", message );
  return message;
}

static int
shutdown_session( lua_host *h )
{
  size_t errors = h->diagnostic_errors;
  h->closed = 1;
  fuse_shutdown();
  h->ready = 0;
  host_services_end();
  if( h->diagnostic_errors == errors ) return 0;
  fprintf( stderr, "error during emulator shutdown\n" );
  return 1;
}

static int
execute_session( lua_host *h )
{
  lua_State *L = create_lua_state( h );
  const char *message;
  int status;
  if( !L ) return 1;
  message = execute_lua( L, h );
  message = capture_failure( h, message );
  status = lua_host_write_result( h, message ) || h->failure != NULL;
  if( shutdown_session( h ) ) status = 1;
  lua_close( L );
  return status;
}

static void
destroy_host( lua_host *h )
{
  size_t i;
  for( i = 0; i < h->diagnostic_count; i++ ) free( h->diagnostics[i].text );
  free( h );
}

int
main( int argc, char **argv )
{
  lua_host *h;
  int status;
  if( argc == 2 && !strcmp( argv[1], "--help" ) ) {
    lua_host_usage(); return 0;
  }
  if( !lua_host_script_mode( argc, argv ) ) return fuse_legacy_main( argc,
                                                                     argv );
  h = calloc( 1, sizeof( *h ) );
  if( !h ) return 1;
  status = lua_host_parse_options( h, argc, argv );
  if( !status ) status = execute_session( h );
  destroy_host( h );
  return status ? 1 : 0;
}
