/* fuse-internal.h: private application/engine boundary
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

#ifndef FUSE_INTERNAL_H
#define FUSE_INTERNAL_H

/* Private, UI-bound, exclusive legacy-runtime lifecycle. Not a supported
   embedding API: selected toolkit/fatal-error policy still applies. Calls must
   be serialized on the frontend owner thread; there is no restart promise.
   An accepted prepare (including help/version or failure) consumes one attempt.
   Invalid arguments do not consume it. argv and its mutable strings are borrowed
   through shutdown; argc == 0 with argv == NULL is permitted. */
typedef enum fuse_legacy_init_result {
  FUSE_LEGACY_INIT_READY,
  FUSE_LEGACY_INIT_HELP,
  FUSE_LEGACY_INIT_VERSION,
  FUSE_LEGACY_INIT_FAILED,
  FUSE_LEGACY_INIT_BUSY,
  FUSE_LEGACY_INIT_CLOSED,
  FUSE_LEGACY_INIT_INVALID_ARGUMENT
} fuse_legacy_init_result;

/* Help/version and failure return after cleanup, without a runnable session.
   Core preparation/shutdown never authorize automatic preference saving, even
   when the autosave setting is enabled. Explicit settings operations remain
   available; RNG, platform startup and library management are unchanged. */
fuse_legacy_init_result fuse_legacy_prepare( int argc, char **argv );

typedef enum fuse_legacy_run_result {
  FUSE_LEGACY_RUN_COMPLETED,
  FUSE_LEGACY_RUN_PAUSED,
  FUSE_LEGACY_RUN_EXITING,
  FUSE_LEGACY_RUN_BUSY,
  FUSE_LEGACY_RUN_NOT_READY
} fuse_legacy_run_result;

/* Advance to the next completed frame boundary (not necessarily a whole frame
   from the current position). Repeatable after READY, on the owner thread only.
   Returns after CPU/due-event dispatch, including the frame's interrupt/RZX/UI
   companions. Existing services/debugger may block without a latency bound.
   EXITING takes precedence over PAUSED, then COMPLETED. A paused call advances
   no emulated time; the host owns command handling and waiting between calls.
   Ordinary reset may occur during dispatch and does not cancel the turn.
   Recursive calls are BUSY; before prepare/after legacy execute or shutdown,
   calls are NOT_READY. No automatic shutdown or application result writing. */
fuse_legacy_run_result fuse_legacy_run_frame( void );

/* Run the blocking legacy loop once after READY, optionally after host turns.
   Zero means accepted; the application exit status is written separately.
   Rejection leaves it alone. This finalizes application automation output once;
   subsequent frame/legacy execution is rejected. Not cooperative execution. */
int fuse_legacy_execute( int *exit_status );

/* End a prepared/completed session. No-op before preparation or after shutdown,
   and during preparation, execution or cleanup callbacks. Request exit through
   existing frontend mechanisms rather than tearing down an active operation. */
void fuse_legacy_shutdown( void );

/* Compatible application runner, without a platform entry point. Authorizes
   the existing autosave preference behavior only after successful preparation. */
int fuse_legacy_main( int argc, char **argv );

#endif
