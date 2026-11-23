/* fuse-operations.c: complete engine operations for host callers
   Copyright (c) 1999-2026 Philip Kendall and others
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

#include <string.h>

#include "display.h"
#include "event.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "input.h"
#include "keyboard.h"
#include "machine.h"
#include "movie.h"
#include "peripherals/dck.h"
#include "peripherals/disk/didaktik.h"
#include "peripherals/disk/disciple.h"
#include "peripherals/if2.h"
#include "peripherals/ide/divmmc.h"
#include "peripherals/ide/simpleide.h"
#include "peripherals/ide/zxatasp.h"
#include "peripherals/ide/divide.h"
#include "peripherals/ide/zxmmc.h"
#include "peripherals/ide/zxcf.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "peripherals/multiface.h"
#include "rzx.h"
#include "settings.h"
#include "snapshot.h"
#include "tape.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"

static int
cartridge_available( fuse_cartridge_slot slot )
{
  switch( slot ) {
  case FUSE_CARTRIDGE_INTERFACE2:
    return periph_is_active( PERIPH_TYPE_INTERFACE2 );
  case FUSE_CARTRIDGE_TIMEX_DOCK:
    return !!( machine_current->capabilities &
               LIBSPECTRUM_MACHINE_CAPABILITY_TIMEX_DOCK );
  }
  return -1;
}

static int
cartridge_loaded( fuse_cartridge_slot slot )
{
  return slot == FUSE_CARTRIDGE_INTERFACE2 ? if2_active : dck_active;
}

void
fuse_cartridge_info_free( fuse_cartridge_info *info )
{
  if( !info ) return;
  libspectrum_free( info->filename );
  libspectrum_free( info );
}

fuse_host_operation_result
fuse_host_copy_cartridge_info( fuse_cartridge_slot slot,
                               fuse_cartridge_info **info )
{
  const char *filename;
  int available;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( !info ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *info = NULL;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  available = cartridge_available( slot );
  if( available < 0 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  *info = libspectrum_new0( fuse_cartridge_info, 1 );
  (*info )->available = available;
  (*info )->loaded = available && cartridge_loaded( slot );
  if( (*info )->loaded ) {
    filename = slot ==
               FUSE_CARTRIDGE_INTERFACE2 ? if2_attachment_path() :
               settings_current.dck_file;
    if( filename ) (*info )->filename = utils_safe_strdup( filename );
  }
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
check_cartridge_operation( fuse_cartridge_slot slot )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  int available;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  available = cartridge_available( slot );
  if( available < 0 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() !=
      FUSE_DECISION_NONE ) return FUSE_HOST_OPERATION_BUSY;
  return available ? FUSE_HOST_OPERATION_OK : FUSE_HOST_OPERATION_MEDIA_ERROR;
}

fuse_host_operation_result
fuse_host_insert_cartridge( fuse_cartridge_slot slot, const char *filename )
{
  fuse_host_operation_result result = check_cartridge_operation( slot );
  utils_file file;
  int error;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  utils_file_init( &file, filename );
  error = utils_file_read( &file );
  if( !error && slot == FUSE_CARTRIDGE_INTERFACE2 && file.length != 0x4000 ) {
    ui_error( UI_ERROR_ERROR, "Interface 2 cartridge must be 16384 bytes" );
    error = 1;
  }
  if( error ) {
    utils_file_free( &file );
    fuse_emulation_unpause();
    fuse_engine_end_operation();
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  }
  error = slot ==
          FUSE_CARTRIDGE_INTERFACE2 ? if2_insert_loaded( &file ) :
          dck_insert_loaded( &file );
  utils_file_free( &file );
  fuse_emulation_unpause();
  result = fuse_engine_end_machine_operation( error );
  if( result == FUSE_HOST_OPERATION_OK && !cartridge_loaded( slot ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return result;
}

fuse_host_operation_result
fuse_host_eject_cartridge( fuse_cartridge_slot slot )
{
  fuse_host_operation_result result = check_cartridge_operation( slot );
  int error;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = slot == FUSE_CARTRIDGE_INTERFACE2 ? if2_eject() : dck_eject();
  return fuse_engine_end_machine_operation( error );
}

static void
release_physical_input( void )
{
  int button;
  input_cancel_physical();
  for( button = 0; button < 8; button++ )
    kempmouse_update( 0, 0, button, 0 );
}

fuse_host_operation_result
fuse_host_select_machine( const char *id )
{
  int i, error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !id ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( snapshot_rzx_rollback_pending() ) return FUSE_HOST_OPERATION_BUSY;
  for( i = 0; i < machine_count; i++ )
    if( !strcmp( id, machine_types[i]->id ) ) break;
  if( i == machine_count ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  /* ID selection avoids the legacy enum/snapshot fallback and abort policy. */
  error = machine_select_id( machine_types[i]->id );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_reset( fuse_host_reset_kind kind )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( kind != FUSE_HOST_RESET_SOFT && kind != FUSE_HOST_RESET_HARD )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = machine_reset( kind == FUSE_HOST_RESET_HARD );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_open_file( const char *filename, fuse_open_request *request )
{
  int error;
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = (fuse_open_request){ 0 };
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  fuse_engine_begin_operation();
  error = utils_open_file_with_decision( filename, tape_can_autoload(), request );
  if( !request->id ) display_refresh_all();
  fuse_engine_end_operation();
  if( error ) return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return request->id ? FUSE_HOST_OPERATION_NEEDS_DECISION :
         FUSE_HOST_OPERATION_OK;
}

static fuse_quit_request pending_quit;
static fuse_host_operation_result mass_storage_quit_next(
  fuse_quit_request *next );

int
fuse_host_quit_pending( void )
{
  return pending_quit.id != 0;
}

void
fuse_host_clear_quit( void )
{
  pending_quit = (fuse_quit_request){ 0 };
}

static fuse_host_operation_result
continue_quit( fuse_quit_request *next, int check_tape )
{
  fuse_host_operation_result result = FUSE_HOST_OPERATION_OK;

  if( check_tape ) result = fuse_host_request_tape_eject( &next->id );
  if( result == FUSE_HOST_OPERATION_OK ) result = fuse_disk_quit_next( next );
  if( result ==
      FUSE_HOST_OPERATION_OK ) result = fuse_microdrive_quit_next( next );
  if( result ==
      FUSE_HOST_OPERATION_OK ) result = mass_storage_quit_next( next );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) pending_quit = *next;
  if( result != FUSE_HOST_OPERATION_OK ) return result;

  fuse_exiting = 1;
  return FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN;
}

fuse_host_operation_result
fuse_host_request_quit( fuse_quit_request *request )
{
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = (fuse_quit_request){ 0 };
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  return continue_quit( request, 1 );
}

fuse_host_operation_result
fuse_host_answer_quit( fuse_media_decision_id id, fuse_media_decision_choice choice,
                       const char *path, fuse_quit_request *next )
{
  fuse_quit_request current = pending_quit;
  fuse_host_operation_result result;

  if( !next ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *next = (fuse_quit_request){ 0 };
  if( !id || id != current.id ) {
    result = fuse_engine_check_operation();
    return result == FUSE_HOST_OPERATION_OK || current.id ?
      FUSE_HOST_OPERATION_STALE : result;
  }

  if( current.media == FUSE_QUIT_MASS_STORAGE && path )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  /* The existing guarded media entry owns this synchronous step. */
  fuse_host_clear_quit();
  switch( current.media ) {
  case FUSE_QUIT_MASS_STORAGE:
    result = fuse_host_answer_mass_storage_eject( id, choice );
    break;
  case FUSE_QUIT_MICRODRIVE:
    result = fuse_host_answer_microdrive_eject( id, choice, path );
    break;
  case FUSE_QUIT_FLOPPY:
    result = fuse_host_answer_disk_eject( id, choice, path,
                                          &(fuse_open_request){ 0 } );
    break;
  default:
    result = fuse_host_answer_tape_eject( id, choice, path );
    break;
  }
  if( result == FUSE_HOST_OPERATION_INVALID_ARGUMENT || result == FUSE_HOST_OPERATION_BUSY )
    pending_quit = current;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  return continue_quit( next, 0 );
}

static int
mass_storage_target( periph_type controller, libspectrum_ide_unit unit )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: case PERIPH_TYPE_ZXATASP: case PERIPH_TYPE_DIVIDE:
    return unit == LIBSPECTRUM_IDE_MASTER || unit == LIBSPECTRUM_IDE_SLAVE;
  case PERIPH_TYPE_ZXCF: case PERIPH_TYPE_DIVMMC: case PERIPH_TYPE_ZXMMC:
    return unit == LIBSPECTRUM_IDE_MASTER;
  default: return 0;
  }
}

static const char *
mass_storage_name( periph_type controller, libspectrum_ide_unit unit )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return unit ==
           LIBSPECTRUM_IDE_MASTER ? settings_current.simpleide_master_file :
           settings_current.simpleide_slave_file;
  case PERIPH_TYPE_ZXATASP: return unit ==
           LIBSPECTRUM_IDE_MASTER ? settings_current.zxatasp_master_file :
           settings_current.zxatasp_slave_file;
  case PERIPH_TYPE_DIVIDE: return unit ==
           LIBSPECTRUM_IDE_MASTER ? settings_current.divide_master_file :
           settings_current.divide_slave_file;
  case PERIPH_TYPE_ZXCF: return settings_current.zxcf_pri_file;
  case PERIPH_TYPE_DIVMMC: return settings_current.divmmc_file;
  case PERIPH_TYPE_ZXMMC: return settings_current.zxmmc_file;
  default: return NULL;
  }
}

static int
mass_storage_attached( periph_type controller, libspectrum_ide_unit unit )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return simpleide_backing_attached( unit );
  case PERIPH_TYPE_ZXATASP: return zxatasp_backing_attached( unit );
  case PERIPH_TYPE_DIVIDE: return divide_backing_attached( unit );
  case PERIPH_TYPE_ZXCF: return zxcf_backing_attached();
  case PERIPH_TYPE_DIVMMC: return divmmc_backing_attached();
  case PERIPH_TYPE_ZXMMC: return zxmmc_backing_attached();
  default: return 0;
  }
}

static int
mass_storage_eject( periph_type controller, libspectrum_ide_unit unit,
                    int save )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return simpleide_eject_authorized( unit, save );
  case PERIPH_TYPE_ZXATASP: return zxatasp_eject_authorized( unit, save );
  case PERIPH_TYPE_DIVIDE: return divide_eject_authorized( unit, save );
  case PERIPH_TYPE_ZXCF: return zxcf_eject_authorized( save );
  case PERIPH_TYPE_DIVMMC: return divmmc_eject_authorized( save );
  case PERIPH_TYPE_ZXMMC: return zxmmc_eject_authorized( save );
  default: return 1;
  }
}

static int
mass_storage_insert( periph_type controller, libspectrum_ide_unit unit,
                     const char *filename )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return simpleide_insert( filename, unit );
  case PERIPH_TYPE_ZXATASP: return zxatasp_insert( filename, unit );
  case PERIPH_TYPE_DIVIDE: return divide_insert( filename, unit );
  case PERIPH_TYPE_ZXCF: return zxcf_insert( filename );
  case PERIPH_TYPE_DIVMMC: return divmmc_insert( filename );
  case PERIPH_TYPE_ZXMMC: return zxmmc_insert( filename );
  default: return 1;
  }
}

static int
mass_storage_commit( periph_type controller, libspectrum_ide_unit unit )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return simpleide_commit( unit );
  case PERIPH_TYPE_ZXATASP: return zxatasp_commit( unit );
  case PERIPH_TYPE_DIVIDE: return divide_commit( unit );
  case PERIPH_TYPE_ZXCF: return zxcf_commit();
  case PERIPH_TYPE_DIVMMC: divmmc_commit(); return 0;
  case PERIPH_TYPE_ZXMMC: zxmmc_commit(); return 0;
  default: return 1;
  }
}

fuse_host_operation_result
fuse_host_copy_mass_storage_info( periph_type controller,
                                  libspectrum_ide_unit unit,
                                  fuse_mass_storage_info **info )
{
  fuse_mass_storage_info *copy;
  const char *name;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( info ) *info = NULL;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !info || !mass_storage_target( controller, unit ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  name = mass_storage_attached( controller,
                                unit ) ? mass_storage_name( controller,
                                                            unit ) : NULL;
  copy = libspectrum_new( fuse_mass_storage_info, 1 );
  copy->controller = controller;
  copy->unit = unit;
  copy->active = periph_is_active( controller );
  copy->backing_name = name ? utils_safe_strdup( name ) : NULL;
  *info = copy;
  return FUSE_HOST_OPERATION_OK;
}

void
fuse_mass_storage_info_free( fuse_mass_storage_info *info )
{
  if( !info ) return;
  libspectrum_free( info->backing_name );
  libspectrum_free( info );
}

static int
mass_storage_dirty( periph_type controller, libspectrum_ide_unit unit )
{
  switch( controller ) {
  case PERIPH_TYPE_SIMPLEIDE: return simpleide_dirty( unit );
  case PERIPH_TYPE_ZXATASP: return zxatasp_dirty( unit );
  case PERIPH_TYPE_DIVIDE: return divide_dirty( unit );
  case PERIPH_TYPE_ZXCF: return zxcf_has_dirty_media();
  case PERIPH_TYPE_DIVMMC: return divmmc_has_dirty_media();
  case PERIPH_TYPE_ZXMMC: return zxmmc_has_dirty_media();
  default: return 0;
  }
}

typedef enum mass_storage_intent {
  MASS_STORAGE_EXPLICIT, MASS_STORAGE_OPEN, MASS_STORAGE_QUIT
} mass_storage_intent;

static struct {
  periph_type controller;
  libspectrum_ide_unit unit;
  char *filename;
  mass_storage_intent intent;
} pending_mass_storage;

void
fuse_mass_storage_cancel_pending( void )
{
  libspectrum_free( pending_mass_storage.filename );
  pending_mass_storage.filename = NULL;
  fuse_engine_retire_decision( FUSE_DECISION_MASS_STORAGE );
}

static fuse_host_operation_result
finish_mass_storage( periph_type controller, libspectrum_ide_unit unit,
                     const char *filename, int save )
{
  /* Check eject separately: legacy insert masks a refused eject as success. */
  if( ( !filename || mass_storage_name( controller, unit ) ||
        mass_storage_attached( controller, unit ) ) &&
      mass_storage_eject( controller, unit, save ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  if( filename && mass_storage_insert( controller, unit, filename ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
request_mass_storage( periph_type controller, libspectrum_ide_unit unit,
                      const char *filename, mass_storage_intent intent,
                      fuse_media_decision_id *request )
{
  if( !mass_storage_dirty( controller, unit ) )
    return finish_mass_storage( controller, unit, filename, 0 );

  *request = fuse_engine_request_decision( FUSE_DECISION_MASS_STORAGE );
  pending_mass_storage.controller = controller;
  pending_mass_storage.unit = unit;
  pending_mass_storage.filename = utils_safe_strdup( filename );
  pending_mass_storage.intent = intent;
  return FUSE_HOST_OPERATION_NEEDS_DECISION;
}

int
fuse_mass_storage_request_open( periph_type controller, const char *filename,
                                fuse_open_request *request )
{
  fuse_host_operation_result result = request_mass_storage(
    controller, LIBSPECTRUM_IDE_MASTER, filename, MASS_STORAGE_OPEN,
    &request->id );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
    request->question = FUSE_OPEN_SAVE_MASS_STORAGE;
    request->detail.mass_storage.controller = controller;
    request->detail.mass_storage.unit = LIBSPECTRUM_IDE_MASTER;
  }
  return result == FUSE_HOST_OPERATION_MEDIA_ERROR;
}

static fuse_host_operation_result
mass_storage_quit_next( fuse_quit_request *next )
{
  static const struct {
    periph_type controller;
    libspectrum_ide_unit unit;
  } order[] = {
    { PERIPH_TYPE_SIMPLEIDE, LIBSPECTRUM_IDE_MASTER },
    { PERIPH_TYPE_SIMPLEIDE, LIBSPECTRUM_IDE_SLAVE },
    { PERIPH_TYPE_ZXATASP, LIBSPECTRUM_IDE_MASTER },
    { PERIPH_TYPE_ZXATASP, LIBSPECTRUM_IDE_SLAVE },
    { PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER },
    { PERIPH_TYPE_DIVIDE, LIBSPECTRUM_IDE_MASTER },
    { PERIPH_TYPE_DIVIDE, LIBSPECTRUM_IDE_SLAVE },
    { PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER },
    { PERIPH_TYPE_ZXMMC, LIBSPECTRUM_IDE_MASTER }
  };
  size_t i;
  fuse_host_operation_result result = FUSE_HOST_OPERATION_OK;
  fuse_engine_begin_operation();
  for( i = 0; i < sizeof( order ) / sizeof( *order ); i++ ) {
    periph_type controller = order[i].controller;
    libspectrum_ide_unit unit = order[i].unit;
    if( !mass_storage_name( controller, unit ) ) continue;
    result = request_mass_storage( controller, unit, NULL, MASS_STORAGE_QUIT,
                                   &next->id );
    if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
      next->media = FUSE_QUIT_MASS_STORAGE;
      next->mass_storage_controller = controller;
      next->mass_storage_unit = unit;
      next->has_save_target = 1;
    }
    if( result != FUSE_HOST_OPERATION_OK ) break;
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_answer_mass_storage_eject( fuse_media_decision_id id,
                                     fuse_media_decision_choice choice )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  periph_type controller = pending_mass_storage.controller;
  libspectrum_ide_unit unit = pending_mass_storage.unit;
  mass_storage_intent intent = pending_mass_storage.intent;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  result = fuse_engine_validate_decision( FUSE_DECISION_MASS_STORAGE, id,
                                          choice,
                                          NULL, 0 );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  if( choice == FUSE_MEDIA_DECISION_CANCEL ) {
    result = FUSE_HOST_OPERATION_CANCELLED;
  } else if( !mass_storage_attached( controller, unit ) ||
             ( intent == MASS_STORAGE_EXPLICIT &&
               !periph_is_active( controller ) ) ){
    result = FUSE_HOST_OPERATION_STALE;
  } else {
    if( intent == MASS_STORAGE_EXPLICIT ) fuse_emulation_pause();
    result = finish_mass_storage( controller, unit,
                                  pending_mass_storage.filename,
                                  choice == FUSE_MEDIA_DECISION_SAVE );
    if( intent == MASS_STORAGE_EXPLICIT ) fuse_emulation_unpause();
  }
  if( intent == MASS_STORAGE_OPEN ) display_refresh_all();
  fuse_mass_storage_cancel_pending();
  fuse_engine_end_operation();
  return result;
}

static fuse_host_operation_result
change_mass_storage( periph_type controller, libspectrum_ide_unit unit,
                     const char *filename, fuse_media_decision_id *request )
{
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = 0;
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !mass_storage_target( controller, unit ) || ( filename && !*filename ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() !=
      FUSE_DECISION_NONE ) return FUSE_HOST_OPERATION_BUSY;
  if( !periph_is_active( controller ) ) return FUSE_HOST_OPERATION_MEDIA_ERROR;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  result = request_mass_storage( controller, unit, filename,
                                 MASS_STORAGE_EXPLICIT, request );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_eject_mass_storage( periph_type controller, libspectrum_ide_unit unit,
                              fuse_media_decision_id *request )
{
  return change_mass_storage( controller, unit, NULL, request );
}

fuse_host_operation_result
fuse_host_insert_mass_storage( periph_type controller,
                               libspectrum_ide_unit unit, const char *filename,
                               fuse_media_decision_id *request )
{
  if( !filename ) {
    fuse_host_operation_result result = fuse_engine_check_operation();
    if( request ) *request = 0;
    return result ==
           FUSE_HOST_OPERATION_OK ? FUSE_HOST_OPERATION_INVALID_ARGUMENT :
           result;
  }
  return change_mass_storage( controller, unit, filename, request );
}

fuse_host_operation_result
fuse_host_commit_mass_storage( periph_type controller,
                               libspectrum_ide_unit unit )
{
  int error = 0;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !mass_storage_target( controller,
                            unit ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() !=
      FUSE_DECISION_NONE ) return FUSE_HOST_OPERATION_BUSY;
  if( !periph_is_active( controller ) ||
      !mass_storage_attached( controller, unit ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  fuse_engine_begin_operation();
  fuse_emulation_pause();
  error = mass_storage_commit( controller, unit );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
check_rzx_operation( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result == FUSE_HOST_OPERATION_OK &&
      fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  return result;
}

static fuse_host_operation_result
host_play_rzx( const char *filename, const char *movie_target,
               fuse_open_request *request )
{
  utils_file file;
  int error;
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = (fuse_open_request){ 0 };
  result = check_rzx_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename || ( movie_target && !*movie_target ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( rzx_recording || rzx_playback || tape_recording ||
      ( movie_target && movie_is_active() ) )
    return FUSE_HOST_OPERATION_BUSY;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  error = utils_read_file( filename, &file );
  if( !error ) error = rzx_request_open( &file, movie_target, request );
  utils_close_file( &file );
  if( !request->id ) display_refresh_all();
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  if( error ) return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return request->id ? FUSE_HOST_OPERATION_NEEDS_DECISION :
         FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_play_rzx( const char *filename, fuse_open_request *request )
{
  return host_play_rzx( filename, NULL, request );
}

fuse_host_operation_result
fuse_host_record_movie_from_rzx( const char *filename, const char *movie_target,
                                 fuse_open_request *request )
{
  if( !movie_target ) {
    if( request ) *request = (fuse_open_request){ 0 };
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  }
  return host_play_rzx( filename, movie_target, request );
}

fuse_host_operation_result
fuse_host_start_rzx_recording( const char *filename, int embed_snapshot )
{
  int error;
  fuse_host_operation_result result = check_rzx_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ||
      ( embed_snapshot != 0 && embed_snapshot != 1 ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( rzx_playback || rzx_recording || tape_recording )
    return FUSE_HOST_OPERATION_BUSY;
  fuse_engine_begin_operation();
  fuse_emulation_pause();
  error = rzx_start_recording( filename, embed_snapshot );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_insert_rzx_snapshot( void )
{
  int error;
  fuse_host_operation_result result = check_rzx_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = rzx_insert_snapshot();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_stop_rzx( void )
{
  int error = 0;
  fuse_host_operation_result result = check_rzx_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  if( rzx_recording ) error = rzx_stop_recording();
  else if( rzx_playback ) error = rzx_stop_playback( 1 );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_finalise_rzx( const char *filename )
{
  int error;
  fuse_host_operation_result result = check_rzx_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( rzx_recording || rzx_playback ) return FUSE_HOST_OPERATION_BUSY;
  fuse_engine_begin_operation();
  fuse_emulation_pause();
  error = rzx_finalise_recording( filename );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

static int
valid_memory_range( size_t start, size_t length )
{
  return start < 0x10000 && length && length <= 0x10000 - start;
}

fuse_host_operation_result
fuse_host_copy_memory( size_t start, size_t length, libspectrum_byte *buffer )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !buffer || !valid_memory_range( start, length ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  utils_copy_memory( start, length, buffer );
  fuse_engine_end_operation();
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
host_binary_file( const char *filename, size_t start, size_t length, int load )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename || !valid_memory_range( start, length ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  if( load ) {
    utils_file file;
    error = utils_read_file( filename, &file );
    if( !error && file.length < length ) error = 1;
    if( !error ) utils_load_binary( start, length, file.buffer );
    utils_close_file( &file );
  } else {
    error = utils_save_binary( start, length, filename );
  }
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_load_binary( const char *filename, size_t start, size_t length )
{
  return host_binary_file( filename, start, length, 1 );
}

fuse_host_operation_result
fuse_host_save_binary( const char *filename, size_t start, size_t length )
{
  return host_binary_file( filename, start, length, 0 );
}

fuse_host_operation_result
fuse_host_save_snapshot_file( const char *filename )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  /* Chosen-target portion of menu_file_savesnapshot; no second chooser or
     overwrite decision. Preserve its pause/unpause side effects and ordering. */
  fuse_emulation_pause();
  error = snapshot_write( filename );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_toggle_tape_play( void )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = tape_toggle_play( 0 );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_stop_tape( void )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = tape_stop();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_rewind_tape( void )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = tape_rewind();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_select_tape_block( int block )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( block < 0 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = tape_select_block( block );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_save_tape_file( const char *filename )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = tape_write( filename );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_start_tape_recording( void )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  /* Explicit pulse capture is the traps-disabled workflow. With traps on,
     ROM SAVE appends decoded blocks automatically, without this command. */
  if( settings_current.tape_traps ) return FUSE_HOST_OPERATION_MEDIA_ERROR;
  fuse_engine_begin_operation();
  error = tape_record_start();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_stop_tape_recording( void )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = tape_record_stop();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_machine_button_available( fuse_machine_button button, int *available )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( !available ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *available = 0;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  switch( button ) {
  case FUSE_MACHINE_BUTTON_MULTIFACE_RED:
    *available = multiface_red_button_available();
    break;
  case FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC:
    *available = !!disciple_available;
    break;
  case FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP:
    *available = !!didaktik80_available;
    break;
  default:
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  }
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_press_machine_button( fuse_machine_button button )
{
  int available;
  fuse_host_operation_result result =
    fuse_host_machine_button_available( button, &available );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !available ) return FUSE_HOST_OPERATION_FAILED;
  switch( button ) {
  case FUSE_MACHINE_BUTTON_MULTIFACE_RED: multiface_red_button(); break;
  case FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC: disciple_snapshot_nmi(); break;
  case FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP: didaktik80_snap_button(); break;
  }
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_nmi( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  event_add( 0, z80_nmi_event );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_keyboard_event( input_key key, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( pressed != 0 && pressed != 1 ) ||
      !( key == INPUT_KEY_Tab || key == INPUT_KEY_Return ||
         key == INPUT_KEY_Escape ||
         ( key >= INPUT_KEY_space && key <= INPUT_KEY_BackSpace ) ||
         key == INPUT_KEY_KP_Enter ||
         ( key >= INPUT_KEY_Up && key <= INPUT_KEY_F12 ) ||
         ( key >= INPUT_KEY_Shift_L && key <= INPUT_KEY_Mode_switch ) ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) input_keypress_emulation( key );
  else input_keyrelease_emulation( key );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_spectrum_key_event( fuse_host_key key, int pressed )
{
  keyboard_key_name target;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( pressed != 0 &&
      pressed != 1 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( key == FUSE_HOST_KEY_ENTER ) target = KEYBOARD_Enter;
  else if( key == FUSE_HOST_KEY_CAPS ) target = KEYBOARD_Caps;
  else if( key == FUSE_HOST_KEY_SYMBOL ) target = KEYBOARD_Symbol;
  else if( key == FUSE_HOST_KEY_SPACE ||
           ( key >= FUSE_HOST_KEY_0 && key <= FUSE_HOST_KEY_9 ) ||
           ( key >= FUSE_HOST_KEY_A && key <= FUSE_HOST_KEY_Z ) )
    target = (keyboard_key_name)key;
  else return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) keyboard_press( target ); else keyboard_release( target );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_joystick_event( fuse_host_joystick joystick,
                          fuse_host_joystick_button button, int pressed )
{
  static const joystick_button buttons[] = {
    JOYSTICK_BUTTON_LEFT, JOYSTICK_BUTTON_RIGHT, JOYSTICK_BUTTON_UP,
    JOYSTICK_BUTTON_DOWN, JOYSTICK_BUTTON_FIRE
  };
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( joystick < FUSE_HOST_JOYSTICK_1 ||
      joystick > FUSE_HOST_JOYSTICK_KEYBOARD ||
      button < FUSE_HOST_JOYSTICK_LEFT || button > FUSE_HOST_JOYSTICK_FIRE ||
      ( pressed != 0 &&
        pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  joystick_press( joystick == FUSE_HOST_JOYSTICK_KEYBOARD ? JOYSTICK_KEYBOARD :
                  joystick == FUSE_HOST_JOYSTICK_1 ? 0 : 1, buttons[button],
                  pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_motion( int dx, int dy )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  /* Reduce before arithmetic: even INT_MIN/MAX cannot overflow the addition. */
  kempmouse_update( dx % 256, dy % 256, -1, 0 );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_event( fuse_host_mouse_button button, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( button != FUSE_HOST_MOUSE_LEFT && button != FUSE_HOST_MOUSE_RIGHT ) ||
      ( pressed != 0 &&
        pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  input_mouse_button_event( button == FUSE_HOST_MOUSE_LEFT ? INPUT_MOUSE_LEFT :
                            INPUT_MOUSE_RIGHT, pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_release_all( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  release_physical_input();
  return FUSE_HOST_OPERATION_OK;
}
