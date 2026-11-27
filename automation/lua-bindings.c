/* lua-bindings.c: Lua operations and library registration.
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
#include <limits.h>
#include <string.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua-host-internal.h"
#include "fuse-host-input.h"
#include "fuse-host-media.h"
#include "fuse-host-settings.h"
#include "host-services.h"

static int
key( lua_State *L )
{
  const char *name = luaL_checkstring( L, 1 );
  fuse_host_key value;
  int down;
  lua_host_require_ready( L ); luaL_checktype( L, 2, LUA_TBOOLEAN );
  down = lua_toboolean( L, 2 );
  if( strlen( name ) == 1 && ( ( name[0] >= 'a' && name[0] <= 'z' ) ||
                               ( name[0] >= '0' &&
                                 name[0] <= '9' ) ) ) value = name[0];
  else if( !strcmp( name, "space" ) ) value = FUSE_HOST_KEY_SPACE;
  else if( !strcmp( name, "enter" ) ) value = FUSE_HOST_KEY_ENTER;
  else if( !strcmp( name, "caps" ) ) value = FUSE_HOST_KEY_CAPS;
  else if( !strcmp( name, "symbol" ) ) value = FUSE_HOST_KEY_SYMBOL;
  else return lua_host_fail( L, "unknown Spectrum key" );
  lua_host_operation( L, "key", fuse_host_spectrum_key_event( value, down ) );
  return 0;
}

static int
read_memory( lua_State *L )
{
  lua_Integer address = luaL_checkinteger( L, 1 );
  lua_Integer count = lua_isnoneornil( L, 2 ) ? 1 : luaL_checkinteger( L, 2 );
  luaL_Buffer buffer;
  char *bytes;
  lua_host_require_ready( L );
  if( address < 0 || address > 65535 || count <= 0 || count > 65536 - address )
    return lua_host_fail( L, "memory range must not wrap" );
  bytes = luaL_buffinitsize( L, &buffer, (size_t)count );
  lua_host_operation( L, "read memory",
                      fuse_host_copy_memory( (size_t)address, (size_t)count,
                                             (libspectrum_byte *)bytes ) );
  luaL_pushresultsize( &buffer, (size_t)count );
  return 1;
}

static int
read8( lua_State *L )
{
  lua_Integer address = luaL_checkinteger( L, 1 );
  libspectrum_byte byte;
  lua_host_require_ready( L );
  if( address < 0 || address > 65535 ) return lua_host_fail( L,
                                                             "invalid address" );
  lua_host_operation( L, "read byte",
                      fuse_host_copy_memory( (size_t)address, 1, &byte ) );
  lua_pushinteger( L, byte ); return 1;
}

static int
registers( lua_State *L )
{
  processor r;
  lua_host_require_ready( L ); r = lua_host_registers_copy( L );
  lua_newtable( L );
#define REG( n, v ) lua_pushinteger( L, ( v ) ); lua_setfield( L, -2, n )
  REG( "af", r.af.w ); REG( "bc", r.bc.w ); REG( "de", r.de.w );
  REG( "hl", r.hl.w );
  REG( "af_alt", r.af_.w ); REG( "bc_alt", r.bc_.w ); REG( "de_alt", r.de_.w );
  REG( "hl_alt", r.hl_.w );
  REG( "pc", r.pc.w ); REG( "sp", r.sp.w ); REG( "ix", r.ix.w );
  REG( "iy", r.iy.w );
  REG( "i", r.i ); REG( "r", ( r.r & 127 ) | r.r7 );
  REG( "interrupt_mode", r.im );
#undef REG
  lua_pushboolean( L, r.iff1 ); lua_setfield( L, -2, "iff1" );
  lua_pushboolean( L, r.iff2 ); lua_setfield( L, -2, "iff2" );
  lua_pushboolean( L, r.halted ); lua_setfield( L, -2, "halted" );
  return 1;
}

static int
expect( lua_State *L )
{
  if( !lua_toboolean( L, 1 ) ) {
    lua_host_context( L )->failure = "assertion-failure";
    return luaL_error( L, "%s",
                       luaL_optstring( L, 2, "scenario assertion failed" ) );
  }
  return 0;
}

static void
opened( lua_State *L, fuse_host_operation_result status,
        fuse_open_request *request )
{
  while( status == FUSE_HOST_OPERATION_NEEDS_DECISION &&
         request->question == FUSE_OPEN_MAP_JOYSTICK )
    status = fuse_host_answer_snapshot_joystick( request->id, NULL, request );
  if( status != FUSE_HOST_OPERATION_OK &&
      status != FUSE_HOST_OPERATION_NEEDS_DECISION )
    lua_host_context( L )->inspectable = 0; /* Generic Open does not promise rollback. */
  lua_host_operation( L,
                      "media operation (unresolved decisions require explicit host policy)",
                      status );
}

static int
open_file( lua_State *L )
{
  fuse_open_request request;
  const char *path = luaL_checkstring( L, 1 );
  lua_host_require_ready( L ); lua_host_resume( L );
  opened( L, fuse_host_open_file( path, &request ), &request ); return 0;
}

static int
play_rzx( lua_State *L )
{
  fuse_open_request request;
  const char *path = luaL_checkstring( L, 1 );
  lua_host_require_ready( L ); lua_host_resume( L ); host_services_rzx_reset();
  opened( L, fuse_host_play_rzx( path, &request ), &request ); return 0;
}

static int
insert_tape( lua_State *L )
{
  fuse_media_decision_id request;
  const char *path = luaL_checkstring( L, 1 );
  lua_host_require_ready( L );
  lua_host_operation( L, "insert tape",
                      fuse_host_insert_tape_file( path, &request ) );
  return 0;
}

static int
insert_disk( lua_State *L )
{
  static const char *const names[] = { "plus3", "beta", "plusd", "opus",
                                       "disciple", "didaktik", NULL };
  const char *name = luaL_checkstring( L, 1 );
  lua_Integer slot = luaL_checkinteger( L, 2 );
  const char *path = luaL_checkstring( L, 3 );
  int controller;
  fuse_open_request request;
  lua_host_require_ready( L );
  for( controller = 0; names[controller] && strcmp( name, names[controller] );
       controller++ ){
  }
  if( !names[controller] || slot < 0 ||
      slot > INT_MAX ) return lua_host_fail( L,
                                             "invalid disk slot" );
  opened( L,
          fuse_host_insert_disk_file( (fuse_disk_controller)controller,
                                      (int)slot, path, &request ), &request );
  return 0;
}

static int
reset( lua_State *L )
{
  const char *kind = luaL_optstring( L, 1, "soft" );
  lua_host_require_ready( L );
  if( strcmp( kind, "soft" ) && strcmp( kind,
                                        "hard" ) ) return lua_host_fail( L,
                                                                         "reset must be soft or hard" );
  lua_host_resume( L );
  lua_host_operation( L, "reset", fuse_host_reset( !strcmp( kind, "hard" ) ?
                                                   FUSE_HOST_RESET_HARD
  :
                                                   FUSE_HOST_RESET_SOFT ) );
  return 0;
}

static int
machine( lua_State *L )
{
  const char *id = luaL_checkstring( L, 1 );
  lua_host_require_ready( L ); lua_host_resume( L );
  lua_host_operation( L, "machine", fuse_host_select_machine( id ) ); return 0;
}

static int
save_snapshot( lua_State *L )
{
  const char *path = luaL_checkstring( L, 1 );
  lua_host_require_ready( L );
  lua_host_operation( L, "save snapshot",
                      fuse_host_save_snapshot_file( path ) );
  return 0;
}

static int
settings( lua_State *L )
{
  static const char *const allowed[] = { "auto_load", "tape_traps", "divmmc",
                                         "interface1", NULL };
  settings_info *copy;
  fuse_host_operation_result status;
  int seen[4], value[4], reset_allowed = 0;
  size_t i;
  lua_host_require_ready( L ); luaL_checktype( L, 1, LUA_TTABLE );
  lua_host_keys( L, 1, allowed );
  for( i = 0; i < 4; i++ ) {
    lua_getfield( L, 1, allowed[i] );
    seen[i] = !lua_isnil( L, -1 );
    if( seen[i] ) luaL_checktype( L, -1, LUA_TBOOLEAN );
    value[i] = lua_toboolean( L, -1 ); lua_pop( L, 1 );
  }
  if( !lua_isnoneornil( L, 2 ) ) {
    luaL_checktype( L, 2, LUA_TBOOLEAN ); reset_allowed = lua_toboolean( L, 2 );
  }
  lua_host_resume( L );
  lua_host_operation( L, "copy settings", fuse_host_copy_settings( &copy ) );
  if( seen[0] ) copy->auto_load = value[0];
  if( seen[1] ) copy->tape_traps = value[1];
  if( seen[2] ) copy->divmmc_enabled = value[2];
  if( seen[3] ) copy->interface1 = value[3];
  status = fuse_host_apply_settings( copy, reset_allowed );
  fuse_host_free_settings( copy );
  lua_host_operation( L, "apply settings (reset permission must be explicit)",
                      status );
  return 0;
}

static int
record_rzx( lua_State *L )
{
  const char *path = luaL_checkstring( L, 1 );
  lua_host_require_ready( L ); lua_host_resume( L ); host_services_rzx_reset();
  lua_host_operation( L, "record RZX",
                      fuse_host_start_rzx_recording( path, 1 ) );
  return 0;
}

static int
tape_play( lua_State *L )
{
  lua_host_require_ready( L );
  lua_host_operation( L, "tape play", fuse_host_toggle_tape_play() );
  return 0;
}

static int
tape_stop( lua_State *L )
{
  lua_host_require_ready( L );
  lua_host_operation( L, "tape stop", fuse_host_stop_tape() );
  return 0;
}

static int
tape_rewind( lua_State *L )
{
  lua_host_require_ready( L );
  lua_host_operation( L, "tape rewind", fuse_host_rewind_tape() );
  return 0;
}

static int
release_all( lua_State *L )
{
  lua_host_require_ready( L );
  lua_host_operation( L, "release input", fuse_host_release_all() );
  return 0;
}

static int
stop_rzx( lua_State *L )
{
  lua_host_require_ready( L );
  lua_host_operation( L, "stop RZX", fuse_host_stop_rzx() );
  return 0;
}

static const luaL_Reg bindings[] = {
  {"start", lua_host_start}, {"run", lua_host_run}, {"key", key},
  {"release_all", release_all},
  {"read8", read8}, {"read_memory", read_memory}, {"registers", registers},
  {"expect", expect}, {"open", open_file}, {"play_rzx", play_rzx},
  {"stop_rzx", stop_rzx},
  {"insert_tape", insert_tape}, {"insert_disk", insert_disk},
  {"tape_play", tape_play},
  {"tape_stop", tape_stop}, {"tape_rewind", tape_rewind}, {"reset", reset},
  {"machine", machine}, {"settings", settings}, {"record_rzx", record_rzx},
  {"save_snapshot", save_snapshot}, {NULL, NULL}
};
void
lua_host_libraries( lua_State *L, lua_host *h, int argc, char **argv )
{
  static const char *const removed[] = { "dofile", "loadfile", "load", "pcall",
                                         "xpcall", NULL };
  size_t i;
  luaL_requiref( L, LUA_GNAME, luaopen_base, 1 ); lua_pop( L, 1 );
  luaL_requiref( L, LUA_TABLIBNAME, luaopen_table, 1 ); lua_pop( L, 1 );
  luaL_requiref( L, LUA_STRLIBNAME, luaopen_string, 1 ); lua_pop( L, 1 );
  luaL_requiref( L, LUA_MATHLIBNAME, luaopen_math, 1 ); lua_pop( L, 1 );
  luaL_requiref( L, LUA_UTF8LIBNAME, luaopen_utf8, 1 ); lua_pop( L, 1 );
  for( i = 0; removed[i]; i++ ) {
    lua_pushnil( L ); lua_setglobal( L, removed[i] );
  }
  lua_newtable( L ); lua_pushlightuserdata( L, h );
  luaL_setfuncs( L, bindings, 1 ); lua_setglobal( L, "fuse" );
  lua_newtable( L ); lua_pushstring( L, h->script ); lua_rawseti( L, -2, 0 );
  for( i = 0; i < (size_t)argc; i++ ) {
    lua_pushstring( L, argv[i] ); lua_rawseti( L, -2, (lua_Integer)i + 1 );
  }
  lua_setglobal( L, "arg" );
}
