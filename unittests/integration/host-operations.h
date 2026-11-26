/* host-operations.h: shared host operation integration test declarations
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

#ifndef FUSE_HOST_OPERATION_TEST_H
#define FUSE_HOST_OPERATION_TEST_H

#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-engine.h"
#include "fuse-legacy.h"
#include "pokefinder/pokemem.h"
#include "spectrum.h"
#include "event.h"
#include "display.h"
#include "keyboard.h"
#include "machine.h"
#include "memory.h"
#include "periph.h"
#include "phantom_typist.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/didaktik.h"
#include "peripherals/disk/disciple.h"
#include "peripherals/multiface.h"
#include "peripherals/joystick.h"
#include "peripherals/ula.h"
#include "peripherals/disk/drive.h"
#include "settings.h"
#include "snapshot.h"
#include "screenshot.h"
#include "peripherals/scld.h"
#include "rzx.h"
#include "movie.h"
#include "tape.h"
#include "tape_internals.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"

#undef NDEBUG
#include <assert.h>

#define OK FUSE_HOST_OPERATION_OK

struct host_test_rom_provider {
  const char *directory;
  int fail, acquired, released, rejected, custom, synthetic_beta,
      synthetic_buttons;
  const char *fail_name;
  unsigned char *bytes;
};
extern struct host_test_rom_provider provider;

extern fuse_disk_drive_info *retained_disk_info;
extern fuse_tape_info *retained_tape_info;

void check_tape_info( void );
void check_tape_deck( void );
void check_tape_deck_internal( int host );
void check_tape_navigation( void );
void tape_memory_prepare( void );
void tape_memory_finish( void );
void check_rom_tape_roundtrip( void );
void check_tape_recording( int host );
void check_trap_recording( void );
void check_snapshot_roundtrip( void );
void check_tape_workflow( void );
void check_tape_autoload( int fail );
int fail_autoload_reset( int hard_reset, libspectrum_snap *snap );
void write_snapshot_joystick_fixture( void );
void check_snapshot_joystick_decisions( void );
void write_joystick_rzx( const char *filename, int embedded );
void check_rzx_open( int embedded );
void check_rzx_movie( void );
void check_rzx_recording_operations( void );
void check_rzx_rollback( void );
void check_rzx_recording_restore( void );
void check_host_poke_finder( void );
void check_host_binary( void );
void check_pok_open( void );
void check_file_open( const char *mode );
void write_disk_fixture( const char *filename );
void check_floppy_merge( void );
void check_open_floppy( void );
void check_lifecycle_disk_file( void );
void check_disk_save( fuse_disk_controller controller );
void check_microdrive( void );
void check_host_quit( int shutdown_pending );
void check_machine_buttons( int keep_fixture );
void check_rs232( void );
void check_cartridge_lifecycle( void );
void check_profile( void );
void check_psg_logging( void );
void check_live_movie( void );
void check_svg_capture( void );
void check_screenshots( void );
fuse_host_operation_result insert_tape_without_decision( const char *filename );
fuse_host_operation_result eject_tape_discarding_changes( void );
fuse_host_operation_result insert_disk_file_without_decision( fuse_disk_controller controller, int slot,
                                   const char *filename );
fuse_host_operation_result answer_disk_eject_without_followup( fuse_media_decision_id id,
                                    fuse_media_decision_choice choice,
                                    const char *path );
fuse_host_operation_result insert_disk_awaiting_eject( fuse_disk_controller controller, const char *path,
                            fuse_media_decision_id *id );
fuse_host_operation_result new_disk_without_decision( fuse_disk_controller controller, int slot );
fuse_host_operation_result eject_disk_without_decision( fuse_disk_controller controller, int slot );
void check_disk_info_blocked( fuse_host_operation_result expected );

#endif
