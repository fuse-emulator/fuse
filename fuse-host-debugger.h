/* fuse-host-debugger.h: opt-in host debugger workflows
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

#ifndef FUSE_HOST_DEBUGGER_H
#define FUSE_HOST_DEBUGGER_H

#include "fuse-host.h"
/* Configured source interface: include the matching build's config.h first
   for the endian-dependent processor register layout. */
#include "debugger/breakpoint.h"
#include "z80/z80.h"

/* Request a manual stop at the next opcode boundary of a host turn. */
fuse_host_operation_result fuse_host_debugger_break( void );

/* Evaluate existing debugger syntax while stopped. An optional disassembly
   address receives -1 unless the command requested a new pane address.
   A parse failure does not undo commands already evaluated. */
fuse_host_operation_result fuse_host_debugger_command( const char *command,
                                                       int *disassembly_address );

/* Decode one instruction from mapped memory while stopped. */
fuse_host_operation_result fuse_host_debugger_disassemble(
  libspectrum_word address, char *text, size_t capacity, size_t *length );

typedef struct fuse_debugger_breakpoint_info {
  size_t id, ignore;
  debugger_breakpoint_type type;
  debugger_breakpoint_life life;
  char *condition;
  union {
    struct {
      char *source; /* NULL for an absolute address. */
      int page;
      libspectrum_word offset;
    } address;
    debugger_breakpoint_port port;
    libspectrum_dword tstates;
    debugger_event_t event;
  } value;
} fuse_debugger_breakpoint_info;

typedef struct fuse_debugger_page_info {
  char *source;
  libspectrum_word address, offset;
  size_t length;
  int page, writable, contended;
} fuse_debugger_page_info;

typedef struct fuse_debugger_event_info {
  libspectrum_dword tstates;
  char *name;
} fuse_debugger_event_info;

typedef struct fuse_debugger_views {
  size_t breakpoint_count, page_count, event_count;
  fuse_debugger_breakpoint_info *breakpoints;
  fuse_debugger_page_info *pages;
  fuse_debugger_event_info *events;
} fuse_debugger_views;

/* Copy while stopped. All data stays valid after Continue and shutdown. */
fuse_host_operation_result fuse_host_debugger_copy_views(
  fuse_debugger_views **views );
void fuse_debugger_views_free( fuse_debugger_views *views );

/* Available at a host debugger stop. Copy by value. */
fuse_host_operation_result fuse_host_debugger_copy_registers(
  processor *registers );

typedef enum fuse_debugger_access {
  FUSE_DEBUGGER_ACCESS_READ,
  FUSE_DEBUGGER_ACCESS_WRITE
} fuse_debugger_access;
typedef struct fuse_debugger_watchpoint {
  fuse_debugger_access access;
  libspectrum_word address;
} fuse_debugger_watchpoint;
/* Copy the triggering memory access at a host watchpoint stop only. */
fuse_host_operation_result fuse_host_debugger_copy_watchpoint(
  fuse_debugger_watchpoint *watchpoint );
/* Leave the stop and bypass the current execute check once. Does not run. */
fuse_host_operation_result fuse_host_debugger_continue( void );
/* Arm a stop at the next opcode boundary after bypassing the current check.
   Does not run. A frame or budget limit can return before the step stops. */
fuse_host_operation_result fuse_host_debugger_step( void );

#endif
