/* fuse-host-media.h: opt-in host media workflows
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

#ifndef FUSE_HOST_MEDIA_H
#define FUSE_HOST_MEDIA_H

#include "fuse-host.h"
#include "fuse-host-input.h"

/* Slots 0–7 correspond to Microdrives 1–8. Filename is owned by the copy. */
typedef struct fuse_microdrive_info {
  int available, inserted, modified, write_protected;
  char *filename;
} fuse_microdrive_info;

fuse_host_operation_result fuse_host_copy_microdrive_info(
  int slot, fuse_microdrive_info **info );
void fuse_microdrive_info_free( fuse_microdrive_info *info );
fuse_host_operation_result fuse_host_insert_microdrive(
  int slot, const char *filename, fuse_media_decision_id *request );
fuse_host_operation_result fuse_host_new_microdrive(
  int slot, fuse_media_decision_id *request );
fuse_host_operation_result fuse_host_eject_microdrive(
  int slot, fuse_media_decision_id *request );
/* Save uses an authorized path, or NULL if the cartridge has a retained
   filename. As in legacy Fuse, saving an unnamed cartridge leaves it unnamed. */
fuse_host_operation_result fuse_host_save_microdrive( int slot,
                                                      const char *filename );
fuse_host_operation_result fuse_host_answer_microdrive_eject(
  fuse_media_decision_id id, fuse_media_decision_choice choice,
  const char *path );
fuse_host_operation_result fuse_host_set_microdrive_write_protected(
  int slot, int write_protected );

typedef enum fuse_open_question {
  FUSE_OPEN_NO_QUESTION,
  FUSE_OPEN_SAVE_TAPE,
  FUSE_OPEN_SAVE_FLOPPY,
  FUSE_OPEN_MERGE_FLOPPY,
  FUSE_OPEN_MAP_JOYSTICK,
  FUSE_OPEN_SAVE_MASS_STORAGE,
  FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT
} fuse_open_question;

/* Copied by value. Use the matching answer for the question. Tape Save needs
   a chosen path; IDE/MMC Save always commits to the attached backing. */
typedef struct fuse_open_request {
  fuse_media_decision_id id;
  fuse_open_question question;
  union {
    struct {
      fuse_disk_controller controller;
      int slot;
      int has_save_target; /* Save may pass NULL to reuse that target. */
    } floppy;
    struct {
      libspectrum_joystick type;
      int inputs;
    } joystick;
    struct {
      periph_type controller;
      libspectrum_ide_unit unit;
    } mass_storage;
  } detail;
} fuse_open_request;

/* NULL declines the mapping, not the snapshot. A valid reply consumes the
   ID; NEEDS_DECISION returns the next question, all other results clear it. */
fuse_host_operation_result fuse_host_answer_snapshot_joystick(
  fuse_media_decision_id id, const fuse_host_joystick *slot,
  fuse_open_request *next );

/* NULL cancels replay. A chosen snapshot can return a joystick question. */
fuse_host_operation_result fuse_host_answer_rzx_external_snapshot(
  fuse_media_decision_id id, const char *filename, fuse_open_request *next );

/* Named RZX operations use the existing codecs and settings. Finalise
   rewrites the supplied file. Stop ends either recording or replay. */
fuse_host_operation_result fuse_host_play_rzx( const char *filename,
                                               fuse_open_request *request );
/* Use the replay questions before opening the authorized FMF target (which
   may be overwritten). A target-open failure leaves replay active, as in
   legacy Fuse. The existing stop-after-RZX preference still applies. */
fuse_host_operation_result fuse_host_record_movie_from_rzx(
  const char *filename, const char *movie_target, fuse_open_request *request );
fuse_host_operation_result fuse_host_start_rzx_recording( const char *filename,
                                                          int embed_snapshot );
/* Snapshot-based starts may return a joystick question. Recording begins
   only after the snapshot is restored. Continue writes back to filename. */
fuse_host_operation_result fuse_host_record_rzx_from_snapshot(
  const char *snapshot, const char *filename, int embed_snapshot,
  fuse_open_request *request );
fuse_host_operation_result fuse_host_continue_rzx_recording(
  const char *filename, fuse_open_request *request );
/* Owned array of frame positions, indexed by snapshot ordinal. Free with
   libspectrum_free(). Select against the current recording, not an old list. */
fuse_host_operation_result fuse_host_copy_rzx_rollback_points( size_t **frames,
                                                               size_t *count );
fuse_host_operation_result fuse_host_rollback_rzx( fuse_open_request *request );
fuse_host_operation_result fuse_host_rollback_rzx_to( size_t point,
                                                      fuse_open_request *request );
fuse_host_operation_result fuse_host_insert_rzx_snapshot( void );
fuse_host_operation_result fuse_host_stop_rzx( void );
fuse_host_operation_result fuse_host_finalise_rzx( const char *filename );

/* File > Open: existing identification/dispatch, current auto_load/ROM
   eligibility, machine fallback and selected-UI services, followed by refresh.
   Path borrowed through return. OK means legacy zero, which can include a
   cancelled/no-op insertion; MEDIA_ERROR means legacy nonzero. No rollback,
   unchanged-state or safe-continuation promise on error; legacy fatal paths
   remain. This is not strict ID selection or the tape-only insertion intent. */
/* NEEDS_DECISION returns a question with a nonzero ID. Other results clear
   the request. */
fuse_host_operation_result fuse_host_open_file( const char *filename,
                                                fuse_open_request *request );

/* Single-drive controllers use MASTER. The backing name is an owned copy,
   NULL when no backing is attached. */
typedef struct fuse_mass_storage_info {
  periph_type controller;
  libspectrum_ide_unit unit;
  int active;
  char *backing_name;
} fuse_mass_storage_info;

fuse_host_operation_result fuse_host_copy_mass_storage_info(
  periph_type controller, libspectrum_ide_unit unit,
  fuse_mass_storage_info **info );
void fuse_mass_storage_info_free( fuse_mass_storage_info *info );
fuse_host_operation_result fuse_host_eject_mass_storage(
  periph_type controller, libspectrum_ide_unit unit,
  fuse_media_decision_id *request );

/* Commit uses the controller's existing reporting; success is not durability. */
fuse_host_operation_result fuse_host_commit_mass_storage(
  periph_type controller, libspectrum_ide_unit unit );
fuse_host_operation_result fuse_host_insert_mass_storage(
  periph_type controller, libspectrum_ide_unit unit, const char *filename,
  fuse_media_decision_id *request );
/* Dirty changes return NEEDS_DECISION. Replacement retains the path, not the
   image bytes. Save commits to the attached backing; no Save As. */
fuse_host_operation_result fuse_host_answer_mass_storage_eject(
  fuse_media_decision_id id, fuse_media_decision_choice choice );

/* Chosen-target Save Snapshot, not File Open or an owned checkpoint/buffer API.
   Borrows a nonempty path through return; engine infers format from the filename
   and retains the existing SZX fallback and conversion-loss warnings. No chooser
   or new overwrite confirmation: an existing target can be truncated/replaced.
   Uses the menu's nested pause/write/unpause sequence (including competition
   RZX finalization on first pause). Nonzero writer result maps MEDIA_ERROR;
   no rollback, durability or complete-state preservation guarantee. */
fuse_host_operation_result fuse_host_save_snapshot_file( const char *filename );

/* Explicit deck controls: toggle is the existing manual Play/Stop intent, not
   File Open/autoload. Stop affects playback, not recording. Rewind succeeds on
   an empty deck; block selection is zero-based and does not implicitly stop.
   A missing block returns MEDIA_ERROR and can reset partial-interval state.
   Named Save borrows its path, keeps encoding in the engine and finalizes any
   recording before serialization; format/IO failure is not a rollback. */
fuse_host_operation_result fuse_host_toggle_tape_play( void );
fuse_host_operation_result fuse_host_stop_tape( void );
fuse_host_operation_result fuse_host_rewind_tape( void );
fuse_host_operation_result fuse_host_select_tape_block( int block );
fuse_host_operation_result fuse_host_save_tape_file( const char *filename );
/* Prompt-free eject. NEEDS_DECISION sets a nonzero ID; all other results
   clear it. Repeated eject returns the same ID; pending replacement is BUSY. */
fuse_host_operation_result fuse_host_request_tape_eject(
  fuse_media_decision_id *request );

/* Zero-based slots. Insert reads the image before handling the outgoing disk;
   it does not autoload. Insert returns a save or side-merge question with a
   nonzero ID on NEEDS_DECISION; other results clear the request.
   Unavailable/disabled drives return MEDIA_ERROR; unknown slots are invalid. */
fuse_host_operation_result fuse_host_insert_disk_file(
  fuse_disk_controller controller, int slot,
  const char *filename, fuse_open_request *request );
/* Save uses a borrowed host-authorized target, or NULL for the retained target.
   No chooser or overwrite prompt. An existing target can be replaced;
   there is no atomic-write guarantee. */
fuse_host_operation_result fuse_host_save_disk_file(
  fuse_disk_controller controller,
  int slot,
  const char *filename );
/* New uses configured geometry and controller insertion hooks (including +3
   preformat). A dirty outgoing disk returns a nonzero ID for answer_disk_eject. */
fuse_host_operation_result fuse_host_new_disk( fuse_disk_controller controller,
                                               int slot,
                                               fuse_media_decision_id *request );
/* Prompt-free eject: NEEDS_DECISION sets a nonzero ID; otherwise clears it. */
fuse_host_operation_result fuse_host_eject_disk(
  fuse_disk_controller controller,
  int slot,
  fuse_media_decision_id *request );
/* SAVE uses the existing target if path is NULL, or a host-authorized path.
   Other choices require NULL. A valid reply consumes the ID. After successful
   eject, a pending Insert may return a side-merge question with a fresh ID.
   Results other than NEEDS_DECISION clear next. */
fuse_host_operation_result fuse_host_answer_disk_eject(
  fuse_media_decision_id request, fuse_media_decision_choice choice,
  const char *path, fuse_open_request *next );
/* Declining the merge loads only the selected side; it does not cancel Open. */
fuse_host_operation_result fuse_host_answer_disk_merge(
  fuse_media_decision_id request, int merge );
fuse_host_operation_result fuse_host_set_disk_flipped(
  fuse_disk_controller controller,
  int slot, int flipped );
fuse_host_operation_result fuse_host_set_disk_write_protected(
  fuse_disk_controller controller,
  int slot,
  int write_protected );
/* Copy on the owner thread between execution turns, including registered but
   inactive/disabled drives. Unknown controller/slot is INVALID_ARGUMENT. *info
   is set to NULL on failure; pass a fresh result pointer (free any prior copy
   before reuse). Success transfers the value/strings to the caller, independent
   of later operations or shutdown. Free with fuse_disk_drive_info_free(). This
   observation is not a command token; mutations still resolve current state. */
fuse_host_operation_result fuse_host_copy_disk_drive_info(
  fuse_disk_controller controller, int slot, fuse_disk_drive_info **info );

#endif
