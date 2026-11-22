/* debugger.c: Fuse's monitor/debugger
   Copyright (c) 2002-2017 Philip Kendall

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

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"
#include "keyboard_refresh.h"

#include "debugger.h"
#include "debugger_internals.h"
#include "event.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "infrastructure/startup_manager.h"
#include "memory_pages.h"
#include "mempool.h"
#include "periph.h"
#include "ui/ui.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

/* The current activity state of the debugger */
enum debugger_mode_t debugger_mode;
static struct z80_host_debug {
  int stopped, skip_execute, stepping, manual_break, resume_past_opcode;
  int running_batch, has_watchpoint;
  libspectrum_word pc;
  fuse_debugger_watchpoint watchpoint;
} host_debug;

static int host_context;
static int *host_disassembly_address;
static void host_continue( void );
static void host_step( void );

int
debugger_host_context( void )
{
  return host_context;
}

void
debugger_host_set_context( int enabled )
{
  host_context = enabled;
}

void
debugger_command_disassemble( libspectrum_word address )
{
  if( !host_context ) ui_debugger_disassemble( address );
  else if( host_disassembly_address ) *host_disassembly_address = address;
}

/* Which base should we display things in */
int debugger_output_base;

/* Memory pool used by the lexer and parser */
int debugger_memory_pool;

/* The event type used for time breakpoints */
int debugger_breakpoint_event;

/* Fuse's exit code */
static int exit_code = 0;

static int
debugger_init( void *context )
{
  debugger_breakpoints = NULL;
  debugger_output_base = 16;

  debugger_memory_pool = mempool_register_pool();

  debugger_breakpoint_event = event_register( debugger_breakpoint_time_fn, "Breakpoint" );

  debugger_event_init();
  debugger_system_variable_init();
  debugger_variable_init();
  debugger_reset();

  return 0;
}

void
debugger_reset( void )
{
  keyboard_refresh_invalidate();
  debugger_breakpoint_remove_all();
  debugger_mode = DEBUGGER_MODE_INACTIVE;
}

static void
debugger_end( void )
{
  host_debug = (struct z80_host_debug){ 0 };
  debugger_breakpoint_remove_all();
  debugger_variable_end();
  debugger_system_variable_end();
  debugger_event_end();
}

void
debugger_register_startup( void )
{
  startup_manager_module dependencies[] = {
    STARTUP_MANAGER_MODULE_EVENT,
    STARTUP_MANAGER_MODULE_MEMPOOL,
    STARTUP_MANAGER_MODULE_SETUID,
  };
  startup_manager_register( STARTUP_MANAGER_MODULE_DEBUGGER, dependencies,
                            ARRAY_SIZE( dependencies ), debugger_init, NULL,
                            debugger_end );
}

/* Activate the debugger */
int
debugger_trap( void )
{
  keyboard_refresh_invalidate();
  return ui_debugger_activate();
}

static void
debugger_arm_step( void )
{
  keyboard_refresh_invalidate();
  debugger_mode = DEBUGGER_MODE_HALTED;
}

/* Step one instruction */
int
debugger_step( void )
{
  if( host_context ) {
    if( host_debug.stopped ) host_step();
    else debugger_arm_step();
  } else {
    debugger_arm_step();
    ui_debugger_deactivate( 0 );
  }
  return 0;
}

/* Step to the next instruction, ignoring CALLs etc */
int
debugger_next( void )
{
  size_t length;

  /* Find out how long the current instruction is */
  debugger_disassemble( NULL, 0, &length, PC );

  /* And add a breakpoint after that */
  debugger_breakpoint_add_address(
    DEBUGGER_BREAKPOINT_TYPE_EXECUTE, memory_source_any, 0, PC + length, 0,
    DEBUGGER_BREAKPOINT_LIFE_ONESHOT, NULL
  );

  debugger_run();

  return 0;
}

/* Set debugger_mode so that emulation will occur */
static void
debugger_resume_execution( void )
{
  keyboard_refresh_invalidate();
  debugger_mode = debugger_breakpoints ?
                  DEBUGGER_MODE_ACTIVE :
                  DEBUGGER_MODE_INACTIVE;
}

int
debugger_run( void )
{
  if( host_context && host_debug.stopped ) host_continue();
  else debugger_resume_execution();
  if( !host_context ) ui_debugger_deactivate( 1 );
  return 0;
}

void
debugger_note_watchpoint( debugger_breakpoint_type type, libspectrum_dword address )
{
  if( !host_debug.running_batch || host_debug.has_watchpoint ) return;
  host_debug.has_watchpoint = 1;
  host_debug.watchpoint.access = type == DEBUGGER_BREAKPOINT_TYPE_READ ?
    FUSE_DEBUGGER_ACCESS_READ : FUSE_DEBUGGER_ACCESS_WRITE;
  host_debug.watchpoint.address = address;
}

int
debugger_before_opcode( libspectrum_word pc, struct z80_host_debug *host )
{
  int execute_check = debugger_mode == DEBUGGER_MODE_ACTIVE;

  if( host && host->skip_execute ) {
    host->skip_execute = 0;
    if( host->pc == pc ) return 0;
  }
  if( !debugger_check( DEBUGGER_BREAKPOINT_TYPE_EXECUTE, pc ) ) return 0;
  if( host ) {
    host->pc = PC;
    host->resume_past_opcode = execute_check ||
                               ( host->stepping && !host->has_watchpoint );
    host->stepping = 0;
    host->manual_break = 0;
    host->stopped = 1;
    return 1;
  }
  debugger_trap();
  return 0;
}

void
debugger_host_run_batch( const libspectrum_dword *deadline )
{
  /* No execute check remains to bypass if breakpoints were removed. */
  if( debugger_mode == DEBUGGER_MODE_INACTIVE ) host_debug.skip_execute = 0;
  host_debug.running_batch = 1;
  z80_do_opcodes_until( deadline, &host_debug );
  host_debug.running_batch = 0;
}

int
debugger_host_stopped( void )
{
  return host_debug.stopped;
}

int
debugger_host_pending( void )
{
  return host_debug.stopped || host_debug.stepping || host_debug.manual_break ||
         host_debug.has_watchpoint;
}

fuse_host_operation_result
fuse_host_debugger_break( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  if( host_debug.stopped ) return FUSE_HOST_OPERATION_OK;
  host_debug.manual_break = 1;
  host_debug.skip_execute = 0;
  debugger_arm_step();
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_debugger_disassemble( libspectrum_word address, char *text,
                                size_t capacity, size_t *length )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !text || !capacity ||
      !length ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( !host_debug.stopped ) return FUSE_HOST_OPERATION_NOT_READY;
  debugger_disassemble( text, capacity, length, address );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_debugger_copy_registers( processor *registers )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !registers ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( !host_debug.stopped ) return FUSE_HOST_OPERATION_NOT_READY;
  *registers = z80;
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_debugger_copy_watchpoint( fuse_debugger_watchpoint *watchpoint )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !watchpoint ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( !host_debug.stopped || !host_debug.has_watchpoint ) return FUSE_HOST_OPERATION_NOT_READY;
  *watchpoint = host_debug.watchpoint;
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_debugger_continue( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !host_debug.stopped ) return FUSE_HOST_OPERATION_NOT_READY;
  host_continue();
  return FUSE_HOST_OPERATION_OK;
}

static void
host_continue( void )
{
  host_debug.stopped = 0;
  debugger_resume_execution();
  host_debug.skip_execute = host_debug.resume_past_opcode &&
                            debugger_mode == DEBUGGER_MODE_ACTIVE;
  host_debug.has_watchpoint = 0;
}

fuse_host_operation_result
fuse_host_debugger_step( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !host_debug.stopped ) return FUSE_HOST_OPERATION_NOT_READY;
  host_step();
  return FUSE_HOST_OPERATION_OK;
}

static void
host_step( void )
{
  debugger_arm_step();
  host_debug.stopped = 0;
  host_debug.skip_execute = 1;
  host_debug.stepping = 1;
  host_debug.has_watchpoint = 0;
}

fuse_host_operation_result
fuse_host_debugger_command( const char *command, int *disassembly_address )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( disassembly_address ) *disassembly_address = -1;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !command || !*command ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  if( !host_debug.stopped ) return FUSE_HOST_OPERATION_NOT_READY;

  fuse_engine_begin_operation();
  debugger_host_set_context( 1 );
  host_disassembly_address = disassembly_address;
  error = debugger_command_evaluate( command );
  host_disassembly_address = NULL;
  debugger_host_set_context( 0 );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_INVALID_ARGUMENT : FUSE_HOST_OPERATION_OK;
}

/* Exit from the last CALL etc by setting a oneshot breakpoint at
   (SP) and then starting emulation */
int
debugger_breakpoint_exit( void )
{
  libspectrum_word target;

  target = readbyte_internal( SP ) + 0x100 * readbyte_internal( SP+1 );

  if( debugger_breakpoint_add_address(
        DEBUGGER_BREAKPOINT_TYPE_EXECUTE, memory_source_any, 0, target, 0,
	DEBUGGER_BREAKPOINT_LIFE_ONESHOT, NULL
      )
    )
    return 1;

  if( debugger_run() ) return 1;

  return 0;
}

/* Poke a value into RAM */
int
debugger_poke( libspectrum_word address, libspectrum_byte value )
{
  writebyte_internal( address, value );
  return 0;
}

/* Write a value to a port */
int
debugger_port_write( libspectrum_word port, libspectrum_byte value )
{
  writeport_internal( port, value );
  return 0;
}

/* Exit the emulator */
void
debugger_exit_emulator( debugger_expression *exit_code_expression )
{
  fuse_exiting = 1;

  exit_code = exit_code_expression ?
    debugger_expression_evaluate( exit_code_expression ) : 0;

  /* Ensure we break out of the main Z80 loop immediately */
  event_add( 0, event_type_null );

  debugger_run();
}

int
debugger_get_exit_code( void )
{
  return exit_code;
}
