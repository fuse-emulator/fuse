/* lua-host-internal.h: private Lua application state and module interfaces.
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

#ifndef FUSE_LUA_HOST_INTERNAL_H
#define FUSE_LUA_HOST_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <lua.h>

#include "fuse-host.h"
#include "fuse-host-debugger.h"
#include "automation.h"

#if LUA_VERSION_NUM < 504 || LUA_VERSION_NUM > 505
#error Lua 5.4 or 5.5 is required
#endif
_Static_assert( LUA_EXTRASPACE >= sizeof( void * ),
                "Lua extra space must hold a pointer" );

#define MAX_PHASES 1024
#define MAX_DIAGNOSTICS 256
#define MAX_ROMS 64
#define LUA_HEAP_LIMIT ( 64u * 1024u * 1024u )

typedef struct rom_entry {
  char name[256], path[4096];
  size_t size;
  unsigned long crc;
  int custom;
} rom_entry;
typedef struct phase {
  const char *termination;
  unsigned long frames, maximum_frames, tstates, hits;
  unsigned int pc;
  int has_pc;
} phase;
typedef struct diagnostic_entry { int level; char *text; } diagnostic_entry;
typedef struct lua_host {
  int attempted, ready, inspectable, closed, stopped, breakpoints;
  int screen, audio, default_screen, default_audio;
  char machine[256], rom_directory[4096];
  const char *output, *script, *failure;
  int argument_count;
  char **arguments;
  rom_entry overrides[MAX_ROMS], roms[MAX_ROMS];
  size_t overrides_count, rom_count;
  diagnostic_entry diagnostics[MAX_DIAGNOSTICS];
  size_t diagnostic_count, diagnostics_dropped, diagnostic_errors;
  phase phases[MAX_PHASES];
  size_t phase_count;
  unsigned long frames, budgets, maximum_frames;
  uint64_t instructions;
  int hook_interval;
  size_t lua_bytes;
} lua_host;

lua_host *lua_host_context( lua_State *L );
int lua_host_fail( lua_State *L, const char *message );
void lua_host_require_ready( lua_State *L );
void lua_host_operation( lua_State *L, const char *name,
                         fuse_host_operation_result status );
void lua_host_keys( lua_State *L, int table, const char *const *allowed );
int lua_host_boolean_field( lua_State *L, int table, const char *key,
                            int fallback );
void lua_host_string_field( lua_State *L, int table, const char *key, char *out,
                            size_t size );
int lua_host_address_field( lua_State *L, const char *name,
                            unsigned int *value );
unsigned long lua_host_count_field( lua_State *L, const char *name,
                                    unsigned long fallback );
processor lua_host_registers_copy( lua_State *L );
void lua_host_resume( lua_State *L );
const char *lua_host_rzx_name( automation_termination_type outcome );
int lua_host_start( lua_State *L );
int lua_host_run( lua_State *L );
void lua_host_libraries( lua_State *L, lua_host *h, int argc, char **argv );
int lua_host_write_result( lua_host *h, const char *message );
int lua_host_parse_options( lua_host *h, int argc, char **argv );
int lua_host_script_mode( int argc, char **argv );
void lua_host_usage( void );

#endif
