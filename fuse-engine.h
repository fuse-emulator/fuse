/* fuse-engine.h: private implementation boundary between engine and host layer
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

#ifndef FUSE_ENGINE_H
#define FUSE_ENGINE_H

#include "fuse-host-media.h"

/* Engine-owned lifecycle/admission. No lifecycle state is shared with callers.
   Machine failures can invalidate the session; ordinary tape-open failure does
   not. These are synchronous implementation hooks, not additional host API. */
fuse_host_operation_result fuse_engine_check_operation( void );
void fuse_engine_begin_operation( void );
fuse_host_operation_result fuse_engine_end_machine_operation( int error );
void fuse_engine_end_operation( void );

/* One outstanding media decision. Domains own payload and invalidation;
   only the owner may retire the slot. Repeat requests return the current ID. */
typedef enum fuse_decision_owner {
  FUSE_DECISION_NONE,
  FUSE_DECISION_TAPE,
  FUSE_DECISION_FLOPPY,
  FUSE_DECISION_FLOPPY_MERGE,
  FUSE_DECISION_SNAPSHOT,
  FUSE_DECISION_MICRODRIVE,
  FUSE_DECISION_MASS_STORAGE,
  FUSE_DECISION_RZX_EXTERNAL
} fuse_decision_owner;
fuse_decision_owner fuse_engine_pending_decision( void );
fuse_media_decision_id fuse_engine_request_decision( fuse_decision_owner owner );
fuse_host_operation_result fuse_engine_validate_decision(
  fuse_decision_owner owner, fuse_media_decision_id id,
  fuse_media_decision_choice choice, const char *path, int save_requires_path );
void fuse_engine_retire_decision( fuse_decision_owner owner );

int fuse_host_quit_pending( void );
void fuse_host_clear_quit( void );
void fuse_mass_storage_cancel_pending( void );
int fuse_mass_storage_request_open( periph_type controller,
                                    const char *filename,
                                    fuse_open_request *request );
fuse_host_operation_result fuse_disk_quit_next( fuse_quit_request *request );
fuse_host_operation_result fuse_microdrive_quit_next(
  fuse_quit_request *request );

int snapshot_request_open( const libspectrum_file *file,
                           fuse_open_request *request );
void snapshot_cancel_pending( void );
int snapshot_request_rzx_restore( libspectrum_snap *snap,
                                  fuse_open_request *request );
int snapshot_request_rzx_external( const char *filename,
                                   fuse_open_request *request );
int rzx_request_open( const libspectrum_file *file, const char *movie_target,
                      fuse_open_request *request );
int rzx_finish_requested_playback( libspectrum_snap *snap, int error );
int rzx_continue_requested_playback( fuse_open_request *request, int error );
void rzx_cancel_pending( void );
int snapshot_request_rzx_recording( libspectrum_snap *snap,
                                    const char *filename,
                                    fuse_open_request *request );
int rzx_finish_requested_recording( int error );
int rzx_finish_rollback( int error );
int snapshot_request_rzx_rollback( libspectrum_snap *snap,
                                   fuse_open_request *request );
int snapshot_rzx_rollback_pending( void );
void snapshot_cancel_rzx_rollback( void );
void rzx_cancel_pending_recording( void );

/* General File Open with host decisions. */
int utils_open_file_with_decision( const char *filename, int autoload,
                                   fuse_open_request *request );
struct ui_media_drive_info_t;
int ui_media_drive_request_open( const struct ui_media_drive_info_t *drive,
                                 libspectrum_file *file, int autoload,
                                 fuse_open_request *request );

/* Only continuous legacy execution pumps the linked UI at frame completion. */
int fuse_engine_pumps_frame_ui( void );

/* Apply the opaque engine configuration at initialization. */
void fuse_engine_apply_config( const fuse_host_config *config );
void fuse_engine_set_host_diagnostics( const fuse_host_config *config );
void fuse_engine_notify_diagnostic( fuse_diagnostic_level level,
                                    const char *message );

/* Borrowed host resource callback storage, active through teardown. */
void fuse_host_begin_resources( const fuse_host_resources *resources );
void fuse_host_end_resources( void );
int fuse_host_resources_active( void );
int fuse_host_acquire_rom( const char *name, fuse_host_rom *rom );

#endif
