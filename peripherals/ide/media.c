/* media.c: complete engine operations for host callers
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

#include "display.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "periph.h"
#include "settings.h"
#include "utils.h"
#include "peripherals/ide/divmmc.h"
#include "peripherals/ide/simpleide.h"
#include "peripherals/ide/zxatasp.h"
#include "peripherals/ide/divide.h"
#include "peripherals/ide/zxmmc.h"
#include "peripherals/ide/zxcf.h"

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

fuse_host_operation_result
fuse_mass_storage_quit_next( fuse_quit_request *next )
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
