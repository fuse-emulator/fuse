/* lua-run.c: validated run requests and host-owned execution policy.
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
#include <string.h>
#include <lauxlib.h>

#include "lua-host-internal.h"
#include "host-services.h"

static void
lua_host_stop_for_inspection( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  fuse_run_result result;
  if( h->stopped ) return;
  lua_host_operation( L, "debugger stop", fuse_host_debugger_break() );
  host_services_arm();
  result = fuse_run_tstates( 1 );
  if( result != FUSE_RUN_DEBUGGER_STOPPED )
    luaL_error( L, "could not stop for inspection (run status %d)",
                (int)result );
  h->stopped = 1;
}

processor
lua_host_registers_copy( lua_State *L )
{
  processor value;
  lua_host_stop_for_inspection( L );
  lua_host_operation( L, "register inspection",
                      fuse_host_debugger_copy_registers( &value ) );
  return value;
}

void
lua_host_resume( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  if( h->breakpoints ) lua_host_stop_for_inspection( L );
  if( h->stopped ) {
    lua_host_operation( L, "clear conditions",
                        fuse_host_debugger_command( "delete", NULL ) );
    h->breakpoints = 0;
    lua_host_operation( L, "continue", fuse_host_debugger_continue() );
    h->stopped = 0;
  }
}

static void
completed_frame( lua_State *L, phase *p )
{
  lua_host *h = lua_host_context( L );
  p->frames++; h->frames++;
}

static void
remaining_frames( lua_State *L, unsigned long requested )
{
  lua_host *h = lua_host_context( L );
  if( requested > h->maximum_frames - h->frames ) {
    h->failure = "resource-limit";
    luaL_error( L, "total frame-request budget exhausted" );
  }
}

static void
run_error( lua_State *L, fuse_run_result result )
{
  lua_host *h = lua_host_context( L );
  h->failure = "engine-error";
  if( result == FUSE_RUN_NOT_READY ) h->inspectable = 0;
  luaL_error( L, "execution stopped unexpectedly (run status %d)",
              (int)result );
}

const char *
lua_host_rzx_name( automation_termination_type outcome )
{
  switch( outcome ) {
  case AUTOMATION_TERMINATION_RZX_END: return "rzx-end";
  case AUTOMATION_TERMINATION_RZX_DESYNC: return "rzx-desynchronisation";
  case AUTOMATION_TERMINATION_RZX_PARSE_ERROR: return "rzx-parse-error";
  case AUTOMATION_TERMINATION_RZX_SNAPSHOT_ERROR: return "rzx-snapshot-error";
  default: return "rzx-aborted";
  }
}

static void
breakpoint( lua_State *L, unsigned int address )
{
  char command[40];
  snprintf( command, sizeof( command ), "break 0x%04x", address );
  lua_host_operation( L, "PC condition",
                      fuse_host_debugger_command( command, NULL ) );
}

typedef enum {
  LUA_RUN_FRAMES, LUA_RUN_TSTATES, LUA_RUN_PC, LUA_RUN_RZX, LUA_RUN_DISK_IDLE
} lua_run_mode;

typedef struct run_request {
  lua_run_mode mode;
  unsigned long frames, tstates, maximum, ignore, idle_frames;
  unsigned int success, failure;
  int has_failure;
} run_request;

static unsigned long
failure_ignore( lua_State *L )
{
  lua_Integer value;
  lua_getfield( L, 1, "failure_pc_ignore" );
  if( lua_isnil( L, -1 ) ) {
    lua_pop( L, 1 );
    return 0;
  }
  value = luaL_checkinteger( L, -1 );
  if( value < 0 || (uint64_t)value > 0x7fffffffUL )
    lua_host_fail( L, "invalid failure_pc_ignore" );
  lua_pop( L, 1 );
  return (unsigned long)value;
}

static void
validate_run_request( lua_State *L, const run_request *r,
                      int has_success, int until_rzx, int until_disk )
{
  int modes = !!r->frames + !!r->tstates + has_success + until_rzx + until_disk;
  int conditional = has_success || until_rzx || until_disk;
  if( modes != 1 || conditional != !!r->maximum ||
      ( r->has_failure && !has_success ) || ( r->ignore && !r->has_failure ) )
    lua_host_fail( L,
                   "run needs frames, tstates, or a condition with max_frames" );
}

static run_request
parse_run_request( lua_State *L )
{
  run_request r = { 0 };
  int has_success, until_rzx, until_disk;
  static const char *const allowed[] = { "frames", "tstates", "success_pc",
                                         "failure_pc", "failure_pc_ignore",
                                         "max_frames", "until_rzx_end",
                                         "until_disk_idle", "disk_idle_frames",
                                         NULL };
  luaL_checktype( L, 1, LUA_TTABLE );
  lua_host_keys( L, 1, allowed );
  r.frames = lua_host_count_field( L, "frames", 0 );
  r.tstates = lua_host_count_field( L, "tstates", 0 );
  r.maximum = lua_host_count_field( L, "max_frames", 0 );
  r.idle_frames = lua_host_count_field( L, "disk_idle_frames", 50 );
  r.ignore = failure_ignore( L );
  has_success = lua_host_address_field( L, "success_pc", &r.success );
  r.has_failure = lua_host_address_field( L, "failure_pc", &r.failure );
  until_rzx = lua_host_boolean_field( L, 1, "until_rzx_end", 0 );
  until_disk = lua_host_boolean_field( L, 1, "until_disk_idle", 0 );
  validate_run_request( L, &r, has_success, until_rzx, until_disk );
  if( r.frames ) r.mode = LUA_RUN_FRAMES;
  else if( r.tstates ) r.mode = LUA_RUN_TSTATES;
  else if( has_success ) r.mode = LUA_RUN_PC;
  else if( until_rzx ) r.mode = LUA_RUN_RZX;
  else r.mode = LUA_RUN_DISK_IDLE;
  return r;
}

static void
execute_frame( lua_State *L, phase *p, int pc_condition )
{
  lua_host *h = lua_host_context( L );
  fuse_run_result result = fuse_run_frame();
  h->stopped = result == FUSE_RUN_DEBUGGER_STOPPED;
  if( result == FUSE_RUN_COMPLETED ) completed_frame( L, p );
  else if( result != FUSE_RUN_DEBUGGER_STOPPED || !pc_condition )
    run_error( L, result );
  if( host_services_error() ) lua_host_require_ready( L );
}

static void
run_frames( lua_State *L, phase *p )
{
  lua_host_resume( L );
  while( p->frames < p->maximum_frames ) execute_frame( L, p, 0 );
}

static void
run_tstates( lua_State *L, phase *p )
{
  fuse_run_result result;
  lua_host_resume( L );
  result = fuse_run_tstates( p->tstates );
  lua_host_context( L )->budgets++;
  if( result != FUSE_RUN_COMPLETED && result != FUSE_RUN_INTERRUPTED )
    run_error( L, result );
  if( host_services_error() ) lua_host_require_ready( L );
  p->termination = result == FUSE_RUN_COMPLETED ? "tstates" : "interrupted";
}

static void
prepare_pc_condition( lua_State *L, const run_request *r )
{
  lua_host_stop_for_inspection( L );
  lua_host_operation( L, "clear conditions",
                      fuse_host_debugger_command( "delete", NULL ) );
  breakpoint( L, r->success );
  if( r->has_failure && r->failure != r->success ) breakpoint( L, r->failure );
  lua_host_context( L )->breakpoints = 1;
}

static int
check_pc_condition( lua_State *L, phase *p, const run_request *r )
{
  processor registers = lua_host_registers_copy( L );
  int stepping;
  p->pc = registers.pc.w;
  /* Failure precedes success, including a shared address. Each phase owns
     its ignore count; an ignored match still permits success at that PC. */
  if( r->has_failure && p->pc == r->failure && p->hits++ >= r->ignore ) {
    p->termination = "failure";
    p->has_pc = 1;
    return 1;
  }
  if( p->pc == r->success ) {
    p->termination = "success";
    p->has_pc = 1;
    return 1;
  }
  /* Step bypasses an ignored manual-entry stop by exactly one instruction. */
  stepping = r->has_failure && p->pc == r->failure;
  lua_host_operation( L, "continue condition",
                      stepping ? fuse_host_debugger_step() :
                      fuse_host_debugger_continue() );
  lua_host_context( L )->stopped = 0;
  return 0;
}

static void
run_pc( lua_State *L, phase *p, const run_request *r )
{
  prepare_pc_condition( L, r );
  while( p->frames < p->maximum_frames ) {
    if( lua_host_context( L )->stopped && check_pc_condition( L, p,
                                                              r ) ) return;
    execute_frame( L, p, 1 );
  }
}

static int
check_rzx_condition( phase *p )
{
  automation_termination_type outcome;
  if( !host_services_rzx_outcome( &outcome ) ) return 0;
  p->termination = lua_host_rzx_name( outcome );
  return 1;
}

static void
run_rzx( lua_State *L, phase *p )
{
  lua_host_resume( L );
  while( p->frames < p->maximum_frames ) {
    if( check_rzx_condition( p ) ) return;
    execute_frame( L, p, 0 );
    if( check_rzx_condition( p ) ) return;
  }
}

static void
run_disk_idle( lua_State *L, phase *p, const run_request *r )
{
  unsigned long idle = 0;
  lua_host_resume( L );
  host_services_disk_reset();
  while( p->frames < p->maximum_frames ) {
    execute_frame( L, p, 0 );
    if( host_services_disk_observed() && !host_services_disk_on() ) idle++;
    else idle = 0;
    if( idle >= r->idle_frames ) {
      p->termination = "disk-idle";
      return;
    }
  }
}

static void
execute_run_request( lua_State *L, phase *p, const run_request *r )
{
  switch( r->mode ) {
  case LUA_RUN_FRAMES: run_frames( L, p ); break;
  case LUA_RUN_TSTATES: run_tstates( L, p ); break;
  case LUA_RUN_PC: run_pc( L, p, r ); break;
  case LUA_RUN_RZX: run_rzx( L, p ); break;
  case LUA_RUN_DISK_IDLE: run_disk_idle( L, p, r ); break;
  }
  if( !strcmp( p->termination, "error" ) )
    p->termination = r->mode == LUA_RUN_FRAMES ? "frames" : "deadline";
}

static void
push_phase_result( lua_State *L, const phase *p )
{
  lua_newtable( L );
  lua_pushstring( L, p->termination ); lua_setfield( L, -2, "termination" );
  lua_pushinteger( L, p->frames ); lua_setfield( L, -2, "frames_completed" );
  if( p->has_pc ) {
    lua_pushinteger( L, p->pc ); lua_setfield( L, -2, "pc" );
  }
}

int
lua_host_run( lua_State *L )
{
  lua_host *h = lua_host_context( L );
  run_request request;
  phase *p;
  lua_host_require_ready( L );
  request = parse_run_request( L );
  remaining_frames( L, request.frames ? request.frames : request.maximum );
  if( h->phase_count == MAX_PHASES ) {
    h->failure = "resource-limit";
    return lua_host_fail( L, "phase limit exceeded" );
  }
  p = &h->phases[h->phase_count++];
  p->termination = "error";
  p->maximum_frames = request.frames ? request.frames : request.maximum;
  p->tstates = request.tstates;
  host_services_arm();
  execute_run_request( L, p, &request );
  push_phase_result( L, p );
  return 1;
}
