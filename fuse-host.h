/* fuse-host.h: source interface for an owner-thread host
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

#ifndef FUSE_HOST_H
#define FUSE_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "libspectrum.h"
#include "fuse-disk.h"
#include "fuse-peripheral.h"
#include "fuse-tape.h"

/* Source subset for host-owned execution. Does not require config.h or
   internal engine declarations. All engine calls belong to one owner thread. */
/* Selected-UI-bound engine lifecycle. This is a source interface, not an ABI:
   toolkit/fatal-error policy still applies. Calls must be serialized on the
   owner thread; no recursion or restart. An accepted prepare, including failure,
   consumes one attempt. Invalid arguments do not consume it.
   HELP/VERSION are shared outcomes used only by the application adapter. */
typedef enum fuse_init_result {
  FUSE_INIT_READY,
  FUSE_INIT_HELP,
  FUSE_INIT_VERSION,
  FUSE_INIT_FAILED,
  FUSE_INIT_BUSY,
  FUSE_INIT_CLOSED,
  FUSE_INIT_INVALID_ARGUMENT
} fuse_init_result;

/* Host preparation uses the linked UI/output hooks but does not initialize
   or tear down the application UI. The host must have its hooks ready before
   preparation and keep them usable through engine shutdown. Configuration
   owns its strings and may be freed after prepare returns. Only one preparation
   attempt is supported, shared with the legacy entry. No CLI or preference I/O.
   This subset configures machine ID, sound, autoload and input mappings; other
   values retain generated defaults internally. Use shared run/shutdown after READY.
   Owner-thread only, no recursion; selected UI, device waits, RNG and process-
   global library/error policy remain. Not toolkit-free. */
typedef struct fuse_host_config fuse_host_config;

fuse_host_config *fuse_host_config_alloc( void );
void fuse_host_config_free( fuse_host_config *config );

int fuse_host_config_set_machine( fuse_host_config *config, const char *id );
int fuse_host_config_set_sound( fuse_host_config *config, int enabled );
int fuse_host_config_set_auto_load( fuse_host_config *config, int enabled );

/* Existing ROM tape-trap setting (default retained). Disable for explicit
   pulse recording; enabled SAVE traps append decoded blocks automatically. */
int fuse_host_config_set_tape_traps( fuse_host_config *config, int enabled );

/* Set DivMMC enablement at preparation; not a runtime toggle. */
int fuse_host_config_set_divmmc( fuse_host_config *config, int enabled );
int fuse_host_config_set_interface1( fuse_host_config *config, int enabled );

typedef enum fuse_diagnostic_level {
  FUSE_DIAGNOSTIC_INFO, FUSE_DIAGNOSTIC_WARNING, FUSE_DIAGNOSTIC_ERROR
} fuse_diagnostic_level;

typedef void (*fuse_host_diagnostic_fn)( void *context,
                                         fuse_diagnostic_level level,
                                         const char *message );

/* Optional notification on the owner thread, from prepare through teardown.
   Copy the message to retain it. Do not block, pump the UI or reenter Fuse.
   Context is borrowed through teardown. NULL disables the callback; host
   sessions still log to stderr without invoking the linked UI handler. */
int fuse_host_config_set_diagnostics( fuse_host_config *config,
                                      fuse_host_diagnostic_fn notify,
                                      void *context );

/* A synchronous ROM result, not a filesystem path. Provider context remains
   borrowed through shutdown, including failed-startup cleanup. On every acquire,
   even failure, release is called once when non-NULL. Bytes are borrowed only
   until release and copied by the engine. No recursive core entry is permitted.
   logical_name is borrowed only for the callback; return 0 for success, nonzero
   for failure. custom must be 0 or 1 and is explicit provenance (affects
   autoload), not a filename comparison.
   A failed request never triggers implicit filesystem search/default fallback. */
typedef struct fuse_host_rom {
  const unsigned char *data;
  size_t length;
  int custom;
  void *context;
  void (*release)( void *context );
} fuse_host_rom;

typedef struct fuse_host_resources {
  void *context;
  int (*acquire_rom)( void *context, const char *logical_name,
                      fuse_host_rom *rom );
} fuse_host_resources;

fuse_init_result fuse_host_prepare( const fuse_host_config *config,
                                    const fuse_host_resources *resources );

/* Synchronous value operations, serialized between execution turns (also while
   paused). They do not advance emulated time; NMI is queued for normal dispatch.
   No recursive entry or source aggregation: the host merges physical sources.
   FAILED machine/reset operations may have partially mutated state: only shutdown
   is permitted afterwards. INVALID_ARGUMENT never mutates state. These entries
   retain selected-UI synchronous notifications, not async/transactional promises. */
typedef enum fuse_host_operation_result {
  FUSE_HOST_OPERATION_OK,
  FUSE_HOST_OPERATION_INVALID_ARGUMENT,
  FUSE_HOST_OPERATION_BUSY,
  FUSE_HOST_OPERATION_NOT_READY,
  FUSE_HOST_OPERATION_EXITING,
  FUSE_HOST_OPERATION_FAILED,
  FUSE_HOST_OPERATION_MEDIA_ERROR,
  FUSE_HOST_OPERATION_NEEDS_DECISION,
  FUSE_HOST_OPERATION_CANCELLED,
  FUSE_HOST_OPERATION_STALE,
  FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN
} fuse_host_operation_result;

/* Nonzero, single-use decision IDs are shared across media operations. */
typedef uint64_t fuse_media_decision_id;
typedef enum fuse_media_decision_choice {
  FUSE_MEDIA_DECISION_CANCEL,
  FUSE_MEDIA_DECISION_DISCARD,
  FUSE_MEDIA_DECISION_SAVE
} fuse_media_decision_choice;

typedef enum fuse_quit_media {
  FUSE_QUIT_TAPE, FUSE_QUIT_FLOPPY, FUSE_QUIT_MICRODRIVE, FUSE_QUIT_MASS_STORAGE
} fuse_quit_media;

/* On NEEDS_DECISION only, id is nonzero. OK is not a quit outcome.
   Slots are zero-based; controller applies only to floppy requests. */
typedef struct fuse_quit_request {
  fuse_media_decision_id id;
  fuse_quit_media media;
  fuse_disk_controller controller;
  int slot;
  int has_save_target; /* Save can pass NULL to reuse the target. */
  periph_type mass_storage_controller;
  libspectrum_ide_unit mass_storage_unit;
} fuse_quit_request;

/* Resolve tape, floppies, Microdrives then IDE/MMC in legacy order.
   READY_TO_SHUT_DOWN prevents more execution; call fuse_shutdown(). */
fuse_host_operation_result fuse_host_request_quit( fuse_quit_request *request );

/* A reply may return the next decision. Other host operations are BUSY while
   Quit awaits a reply; shutdown remains unconditional teardown. */
fuse_host_operation_result fuse_host_answer_quit(
  fuse_media_decision_id id, fuse_media_decision_choice choice,
  const char *path, fuse_quit_request *next );

/* Spectrum keys, not native keysyms, shortcuts or joystick/menu mappings. */
typedef enum fuse_host_key {
  FUSE_HOST_KEY_SPACE = 0x20,
  FUSE_HOST_KEY_0 = 0x30, FUSE_HOST_KEY_1, FUSE_HOST_KEY_2, FUSE_HOST_KEY_3,
  FUSE_HOST_KEY_4, FUSE_HOST_KEY_5, FUSE_HOST_KEY_6, FUSE_HOST_KEY_7,
  FUSE_HOST_KEY_8, FUSE_HOST_KEY_9,
  FUSE_HOST_KEY_A = 0x61, FUSE_HOST_KEY_B, FUSE_HOST_KEY_C, FUSE_HOST_KEY_D,
  FUSE_HOST_KEY_E, FUSE_HOST_KEY_F, FUSE_HOST_KEY_G, FUSE_HOST_KEY_H,
  FUSE_HOST_KEY_I, FUSE_HOST_KEY_J, FUSE_HOST_KEY_K, FUSE_HOST_KEY_L,
  FUSE_HOST_KEY_M, FUSE_HOST_KEY_N, FUSE_HOST_KEY_O, FUSE_HOST_KEY_P,
  FUSE_HOST_KEY_Q, FUSE_HOST_KEY_R, FUSE_HOST_KEY_S, FUSE_HOST_KEY_T,
  FUSE_HOST_KEY_U, FUSE_HOST_KEY_V, FUSE_HOST_KEY_W, FUSE_HOST_KEY_X,
  FUSE_HOST_KEY_Y, FUSE_HOST_KEY_Z,
  FUSE_HOST_KEY_ENTER = 0x100, FUSE_HOST_KEY_CAPS, FUSE_HOST_KEY_SYMBOL
} fuse_host_key;

fuse_host_operation_result fuse_host_spectrum_key_event( fuse_host_key key,
                                                         int pressed );

/* Copy mapped memory into a caller buffer. Length must be nonzero; no wrap. */
fuse_host_operation_result fuse_host_copy_memory( size_t start, size_t length,
                                                  libspectrum_byte *buffer );

/* Explicit insertion without autoload or a linked UI prompt. Read input before
   handling the old tape. NEEDS_DECISION returns a nonzero ID and owns a copy
   of the input and path until the reply; all other results clear the ID.
   BUSY if another tape decision is pending. A decode failure after confirmed
   eject does not restore the old tape. */
fuse_host_operation_result fuse_host_insert_tape_file(
  const char *filename, fuse_media_decision_id *request );
/* A valid reply consumes the request, even on a write failure. SAVE requires
   a nonempty, host-authorized path; other choices require NULL. For a staged
   replacement, successful eject proceeds immediately to decode and insert. */
fuse_host_operation_result fuse_host_answer_tape_eject(
  fuse_media_decision_id request, fuse_media_decision_choice choice,
  const char *filename );
/* Owned copy of current browser rows and transport state. Same owner-thread /
   lifecycle guards as operations; failure sets *info=NULL. No pause, selection,
   playback change or implicit recording finalization. Strings/values survive
   replacement/eject/shutdown. A copy describes then-current state, not a handle
   or authorization for later commands. Recording can be active on an empty deck.
   Archive-info panels/raw block metadata are not represented by these rows. */
fuse_host_operation_result fuse_host_copy_tape_info( fuse_tape_info **info );

typedef enum fuse_machine_button {
  FUSE_MACHINE_BUTTON_MULTIFACE_RED,
  FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC,
  FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP
} fuse_machine_button;

/* Query whether the selected hardware can currently accept the press. */
fuse_host_operation_result fuse_host_machine_button_available(
  fuse_machine_button button, int *available );
/* Queue the device-specific action for normal CPU dispatch; does not run.
   An unavailable button returns FAILED without changing the machine. */
fuse_host_operation_result fuse_host_press_machine_button(
  fuse_machine_button button );

typedef enum fuse_cartridge_slot {
  FUSE_CARTRIDGE_INTERFACE2, FUSE_CARTRIDGE_TIMEX_DOCK
} fuse_cartridge_slot;

typedef struct fuse_cartridge_info {
  int available; /* The selected machine can use this slot. */
  int loaded;    /* Cartridge installed on the selected machine. */
  char *filename; /* NULL for empty slots or snapshot-restored cartridges. */
} fuse_cartridge_info;

/* Explicit insertion/ejection does not select a different machine. Both reset
   the machine (soft for Interface 2, hard for Dock). Read failure leaves the
   old attachment alone; failure after reset begins need not restore it. */
fuse_host_operation_result fuse_host_insert_cartridge(
  fuse_cartridge_slot slot, const char *filename );
fuse_host_operation_result fuse_host_eject_cartridge(
  fuse_cartridge_slot slot );
/* Owned copy independent of subsequent operations and engine shutdown. */
fuse_host_operation_result fuse_host_copy_cartridge_info(
  fuse_cartridge_slot slot, fuse_cartridge_info **info );
void fuse_cartridge_info_free( fuse_cartridge_info *info );

typedef enum fuse_rs232_endpoint {
  FUSE_RS232_RX = 1, FUSE_RS232_TX
} fuse_rs232_endpoint;

typedef struct fuse_rs232_info {
  int available; /* Interface 1 is active and this build supports serial I/O. */
  int connected; /* A native descriptor is attached, even if IF1 is inactive. */
} fuse_rs232_info;

/* Use the existing native serial backend, not the regular-file backend.
   The path is borrowed through return. An unavailable endpoint or failed
   open/setup returns MEDIA_ERROR. A replacement closes the old descriptor
   first; no rollback is promised. Query status after a failed replacement. */
fuse_host_operation_result fuse_host_connect_rs232(
  fuse_rs232_endpoint endpoint,
  const char *path );
fuse_host_operation_result fuse_host_disconnect_rs232(
  fuse_rs232_endpoint endpoint );
/* Copy status by value; does not open or close the endpoint. */
fuse_host_operation_result fuse_host_copy_rs232_info(
  fuse_rs232_endpoint endpoint,
  fuse_rs232_info *info );

/* Record into the engine's tape, including an empty deck; no file sink here.
   Start requires tape traps disabled; enabled traps, missing tape object or
   RZX refusal is MEDIA_ERROR (the existing RZX diagnostic remains). With traps
   disabled, an already-active recorder is success. Stop remains available if
   the setting changes during recording. It is idle-safe and finalizes intervals
   into blocks, independently of playback. A stop
   failure is MEDIA_ERROR but recording has ended, buffers are freed and any
   blocks already appended remain; retry does not recover the discarded tail.
   Save/eject retain their existing implicit recording finalization. */
fuse_host_operation_result fuse_host_start_tape_recording( void );
fuse_host_operation_result fuse_host_stop_tape_recording( void );

typedef enum fuse_run_result {
  FUSE_RUN_COMPLETED,
  FUSE_RUN_PAUSED,
  FUSE_RUN_EXITING,
  FUSE_RUN_BUSY,
  FUSE_RUN_NOT_READY,
  FUSE_RUN_INVALID_ARGUMENT,
  FUSE_RUN_INTERRUPTED,
  FUSE_RUN_NEEDS_DECISION,
  FUSE_RUN_DEBUGGER_STOPPED
} fuse_run_result;

/* Advance to the next completed frame boundary (not necessarily a whole frame
   from the current position). Repeatable after READY, on the owner thread only.
   Returns after CPU/due-event dispatch, including the frame's interrupt/RZX/UI
   companions. Existing services/debugger may block without a latency bound.
   EXITING takes precedence over NEEDS_DECISION at entry, then DEBUGGER_STOPPED,
   then PAUSED. None advances emulated time on entry. An execute breakpoint
   can stop during dispatch before its opcode and before draining due events.
   The host owns command handling and waiting between calls.
   Ordinary reset may occur during dispatch and does not cancel the turn.
   Recursive calls are BUSY; before preparation/after finalization or shutdown,
   calls are NOT_READY. No automatic shutdown or application result writing. */
fuse_run_result fuse_run_frame( void );

/* Advance by a relative emulated T-state budget (1..0x7fffffff), completing
   instructions and due-event/frame dispatch even if they overshoot the budget.
   Frame rebasing and RZX clock normalization are accounted for, without adding
   deadline events to RZX's forced event dispatch. Ordinary reset continues.
   Machine selection, snapshot restoration or debugger clock assignment during
   the call returns INTERRUPTED at a safe boundary; call again in the new state.
   A pending decision blocks entry, as in run_frame. After dispatch, EXITING
   wins over DEBUGGER_STOPPED, then INTERRUPTED, PAUSED and COMPLETED. Invalid budgets (or a
   clock >=0x7fffffff) are rejected without execution. Phase rejection precedes
   argument validation; other lifecycle and blocking rules match run_frame. */
fuse_run_result fuse_run_tstates( unsigned long budget );

/* Set the host's Pause state between turns (0 or 1). Repeating the same
   request is a no-op; Resume does not release a pause held by another operation.
   Pause and Resume can synchronously stop or restart audio. Entering Pause can
   also end competition RZX recording. If finalizing that recording fails,
   return MEDIA_ERROR but remain paused. A timing-reset failure on Resume
   returns FAILED after releasing the host pause. Does not interrupt a turn. */
fuse_host_operation_result fuse_host_set_paused( int paused );

/* End a prepared, completed or failed-operation session. No-op before preparation,
   after shutdown or during active preparation/execution/cleanup callbacks.
   Does not cancel active work; the host must wait for its return before disposal. */
void fuse_shutdown( void );

#endif
