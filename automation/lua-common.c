/* lua-common.c: Lua argument validation and host-operation admission.
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
#include <string.h>
#include <lauxlib.h>

#include "lua-host-internal.h"
#include "host-services.h"

lua_host *
lua_host_context( lua_State *L )
{
  return lua_touserdata( L, lua_upvalueindex( 1 ) );
}

int
lua_host_fail( lua_State *L, const char *message )
{
  lua_host *h = lua_host_context( L );
  if( !h->failure ) h->failure = "script-error";
  return luaL_error( L, "%s", message );
}

void
lua_host_require_ready( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  if( !h->ready || !h->inspectable || h->closed ) luaL_error( L,
                                                              "emulator session is not ready" );
  if( host_services_error() ) {
    h->failure =
      host_services_resource_limit() ? "resource-limit" : "capture-error";
    luaL_error( L, "%s", host_services_error() );
  }
}

void
lua_host_operation( lua_State *L, const char *name,
                    fuse_host_operation_result status )
{
  lua_host *h = lua_host_context( L );
  if( status == FUSE_HOST_OPERATION_OK ) return;
  if( status == FUSE_HOST_OPERATION_FAILED ) h->inspectable = 0;
  if( !h->failure ) h->failure = "engine-error";
  luaL_error( L, "%s failed (host status %d)", name, (int)status );
}

static unsigned long
lua_host_positive( lua_State *L, int index, unsigned long maximum )
{
  lua_Integer n = luaL_checkinteger( L, index );
  if( n <= 0 || (uint64_t)n > maximum ) luaL_error( L,
                                                    "positive bounded integer required" );
  return (unsigned long)n;
}

void
lua_host_keys( lua_State *L, int table, const char *const *allowed )
{
  const char *key;
  size_t i;
  lua_pushnil( L );
  while( lua_next( L, table ) ) {
    if( lua_type( L, -2 ) != LUA_TSTRING ) luaL_error( L,
                                                       "option names must be strings" );
    key = lua_tostring( L, -2 );
    if( strlen( key ) != lua_rawlen( L, -2 ) ) luaL_error( L,
                                                           "option names must not contain NUL" );
    for( i = 0; allowed[i] && strcmp( key, allowed[i] ); i++ ) {
    }
    if( !allowed[i] ) luaL_error( L, "unknown option '%s'", key );
    lua_pop( L, 1 );
  }
}

int
lua_host_boolean_field( lua_State *L, int table, const char *key, int fallback )
{
  int value;
  lua_getfield( L, table, key );
  if( lua_isnil( L, -1 ) ) value = fallback;
  else {
    luaL_checktype( L, -1, LUA_TBOOLEAN );
    value = lua_toboolean( L, -1 );
  }
  lua_pop( L, 1 );
  return value;
}

void
lua_host_string_field( lua_State *L, int table, const char *key, char *out,
                       size_t size )
{
  const char *text;
  size_t length;
  lua_getfield( L, table, key );
  if( !lua_isnil( L, -1 ) ) {
    text = luaL_checklstring( L, -1, &length );
    if( !length || length >= size || strlen( text ) != length )
      luaL_error( L, "invalid or overlong '%s'", key );
    memcpy( out, text, length + 1 );
  }
  lua_pop( L, 1 );
}

int
lua_host_address_field( lua_State *L, const char *name, unsigned int *value )
{
  lua_Integer n;
  lua_getfield( L, 1, name );
  if( lua_isnil( L, -1 ) ) {
    lua_pop( L, 1 ); return 0;
  }
  n = luaL_checkinteger( L, -1 );
  if( n < 0 || n > 65535 ) luaL_error( L, "invalid PC address" );
  *value = (unsigned int)n;
  lua_pop( L, 1 );
  return 1;
}

unsigned long
lua_host_count_field( lua_State *L, const char *name, unsigned long fallback )
{
  unsigned long count;
  lua_getfield( L, 1, name );
  count = lua_isnil( L, -1 ) ? fallback : lua_host_positive( L, -1,
                                                             0x7fffffffUL );
  lua_pop( L, 1 );
  return count;
}
