/* uimedia.c: Disk media UI routines
   Copyright (c) 2013 Alex Badea
   Copyright (c) 2015-2017 Gergely Szasz
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

   Author contact information:

   E-mail: vamposdecampos@gmail.com

*/

#include "config.h"
#include <string.h>

#ifdef HAVE_LIB_GLIB
#include <glib.h>
#endif

#include "display.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "options.h"
#include "peripherals/disk/fdd.h"
#include "ui/ui.h"
#include "ui/uimedia.h"
#include "utils.h"

#define DISK_TRY_MERGE(heads) \
  ( option_enumerate_diskoptions_disk_try_merge() == 2 || \
    ( option_enumerate_diskoptions_disk_try_merge() == 1 && heads == 1 ) )

static GSList *registered_drives = NULL;
/* Registry entries live until drive teardown. Retire the token before their
   attachment is unloaded, or after a successful separate save. */
static const ui_media_drive_info_t *pending_eject_drive;
typedef enum disk_change {
  DISK_CHANGE_EJECT, DISK_CHANGE_INSERT, DISK_CHANGE_NEW
} disk_change;
static disk_change pending_disk_change;
static utils_file pending_disk_file;
static int pending_disk_autoload, pending_disk_file_open, pending_disk_quit;

static void
retire_disk_eject( void )
{
  pending_eject_drive = NULL;
  if( pending_disk_change == DISK_CHANGE_INSERT ) utils_file_free( &pending_disk_file );
  pending_disk_change = DISK_CHANGE_EJECT;
  pending_disk_autoload = pending_disk_file_open = pending_disk_quit = 0;
  fuse_engine_retire_decision( FUSE_DECISION_FLOPPY );
}

static inline int
menu_item_valid( ui_menu_item item )
{
  return item != UI_MENU_ITEM_INVALID;
}

int
ui_media_drive_register( ui_media_drive_info_t *drive )
{
  registered_drives = g_slist_append( registered_drives, drive );
  return 0;
}

void
ui_media_drive_end( void )
{
  GSList *entry;
  retire_disk_eject();
  /* Dispose loaded images without invoking save/eject decisions. The legacy
     application has already handled quit decisions; rollback must not ask. */
  for( entry = registered_drives; entry; entry = entry->next ) {
    ui_media_drive_info_t *drive = entry->data;
    if( drive->fdd ) disk_close( &drive->fdd->disk );
  }
  g_slist_free( registered_drives );
  registered_drives = NULL;
}

struct find_info {
  fuse_disk_controller controller;
  int drive;
};

static gint
find_drive( gconstpointer data, gconstpointer user_data )
{
  const ui_media_drive_info_t *drive = data;
  const struct find_info *info = user_data;

  return !( drive->controller_index == info->controller &&
            drive->drive_index == info->drive );
}

ui_media_drive_info_t *
ui_media_drive_find( fuse_disk_controller controller, int drive )
{
  struct find_info info = {
    /* .controller = */ controller,
    /* .drive = */ drive,
  };
  GSList *item;
  item = g_slist_find_custom( registered_drives, &info, find_drive );
  return item ? item->data : NULL;
}


static gint
any_available( gconstpointer data, gconstpointer user_data )
{
  const ui_media_drive_info_t *drive = data;

  return !( drive->is_available && drive->is_available() );
}

int
ui_media_drive_any_available( void )
{
  GSList *item;
  item = g_slist_find_custom( registered_drives, NULL, any_available );
  return item ? 1 : 0;
}


static void
update_parent_menus( gpointer data, gpointer user_data )
{
  const ui_media_drive_info_t *drive = data;

  if( drive->is_available && menu_item_valid( drive->menu_item_parent ) )
    ui_menu_activate( drive->menu_item_parent, drive->is_available() );
}

void
ui_media_drive_update_parent_menus( void )
{
  g_slist_foreach( registered_drives, update_parent_menus, NULL );
}

static int
maybe_menu_activate( int id, int activate )
{
  if( !menu_item_valid( id ) )
    return 0;
  return ui_menu_activate( id, activate );
}

void
ui_media_drive_update_menus( const ui_media_drive_info_t *drive,
                             unsigned flags )
{
  if( !drive->fdd )
    return;

  if( flags & UI_MEDIA_DRIVE_UPDATE_TOP )
    maybe_menu_activate( drive->menu_item_top,
                         drive->fdd->type != FDD_TYPE_NONE );
  if( flags & UI_MEDIA_DRIVE_UPDATE_EJECT )
    maybe_menu_activate( drive->menu_item_eject, drive->fdd->loaded );
  if( flags & UI_MEDIA_DRIVE_UPDATE_FLIP )
    maybe_menu_activate( drive->menu_item_flip, !drive->fdd->upsidedown );
  if( flags & UI_MEDIA_DRIVE_UPDATE_WP )
    maybe_menu_activate( drive->menu_item_wp, !drive->fdd->wrprot );
}


int
ui_media_drive_flip( fuse_disk_controller controller, int which, int flip )
{
  ui_media_drive_info_t *drive;

  drive = ui_media_drive_find( controller, which );
  if( !drive )
    return -1;
  if( !drive->fdd->loaded )
    return 1;

  fdd_flip( drive->fdd, flip );
  ui_media_drive_update_menus( drive, UI_MEDIA_DRIVE_UPDATE_FLIP );
  return 0;
}

int
ui_media_drive_writeprotect( fuse_disk_controller controller, int which, int wrprot )
{
  ui_media_drive_info_t *drive;

  drive = ui_media_drive_find( controller, which );
  if( !drive )
    return -1;
  if( !drive->fdd->loaded )
    return 1;

  fdd_wrprot( drive->fdd, wrprot );
  ui_media_drive_update_menus( drive, UI_MEDIA_DRIVE_UPDATE_WP );
  return 0;
}


static int
drive_disk_write( const ui_media_drive_info_t *drive, const char *filename,
                  int confirm_overwrite )
{
  int error;

  drive->fdd->disk.type = DISK_TYPE_NONE;
  if( filename == NULL )
    filename = drive->fdd->disk.filename; /* write over original file */
  else if( confirm_overwrite && compat_file_exists( filename ) ) {
    const char *filename1 = strrchr( filename, FUSE_DIR_SEP_CHR );
    filename1 = filename1 ? filename1 + 1 : filename;

    ui_confirm_save_t confirm = ui_confirm_save(
      "%s already exists.\n"
      "Do you want to overwrite it?",
      filename1
    );

    switch( confirm ) {

    case UI_CONFIRM_SAVE_SAVE:
      break;

    case UI_CONFIRM_SAVE_DONTSAVE: return 2;
    case UI_CONFIRM_SAVE_CANCEL: return -1;

    }
  }

  error = disk_write( &drive->fdd->disk, filename );

  if( error != DISK_OK ) {
    ui_error( UI_ERROR_ERROR, "couldn't write '%s' file: %s", filename,
              disk_strerror( error ) );
    return 1;
  }

  if( !drive->fdd->disk.filename ||
      strcmp( filename, drive->fdd->disk.filename ) ) {
    libspectrum_free( drive->fdd->disk.filename );
    drive->fdd->disk.filename = utils_safe_strdup( filename );
  }
  return 0;
}


/* Both save entry paths finish after releasing their own pause. The chooser
   also owns a temporary filename, which must be freed before that release. */
static int
drive_save_finish( const ui_media_drive_info_t *drive, int error )
{
  if( error ) return 1;
  drive->fdd->disk.dirty = 0;
  if( pending_eject_drive == drive ) retire_disk_eject();
  return 0;
}

static int
drive_save( const ui_media_drive_info_t *drive, int saveas )
{
  int err;
  char *filename = NULL, title[80];

  if( drive->fdd->disk.filename == NULL )
    saveas = 1;

  fuse_emulation_pause();

  snprintf( title, sizeof( title ), "Fuse - Write %s", drive->name );
  if( saveas ) {
    filename = ui_get_save_filename( title );
    if( !filename ) {
      fuse_emulation_unpause();
      return 1;
    }
  }

  err = drive_disk_write( drive, filename, 1 );

  if( saveas )
    libspectrum_free( filename );

  fuse_emulation_unpause();
  return drive_save_finish( drive, err );
}

/* The host has already authorized the destination. */
static int
drive_save_authorized( const ui_media_drive_info_t *drive, const char *path )
{
  int error;
  fuse_emulation_pause();
  error = drive_disk_write( drive, path, 0 );
  fuse_emulation_unpause();
  return drive_save_finish( drive, error );
}

int
ui_media_drive_save( fuse_disk_controller controller, int which, int saveas )
{
  ui_media_drive_info_t *drive;

  drive = ui_media_drive_find( controller, which );
  if( !drive )
    return -1;
  return drive_save( drive, saveas );
}

static void
drive_finish_eject( const ui_media_drive_info_t *drive )
{
  if( pending_eject_drive == drive ) retire_disk_eject();
  fdd_unload( drive->fdd );
  disk_close( &drive->fdd->disk );
  ui_media_drive_update_menus( drive, UI_MEDIA_DRIVE_UPDATE_EJECT );
}

static int
drive_eject( const ui_media_drive_info_t *drive )
{
  if( !drive->fdd->loaded )
    return 0;

  if( drive->fdd->disk.dirty ) {

    ui_confirm_save_t confirm = ui_confirm_save(
      "%s has been modified.\n"
      "Do you want to save it?",
      drive->name
    );

    switch( confirm ) {

    case UI_CONFIRM_SAVE_SAVE:
      if( drive_save( drive, 0 ) )
        return 1;   /* first save it...*/
      break;

    case UI_CONFIRM_SAVE_DONTSAVE: break;
    case UI_CONFIRM_SAVE_CANCEL: return 1;

    }
  }

  drive_finish_eject( drive );
  return 0;
}

int
ui_media_drive_eject( fuse_disk_controller controller, int which )
{
  ui_media_drive_info_t *drive;

  drive = ui_media_drive_find( controller, which );
  if( !drive )
    return -1;
  return drive_eject( drive );
}

static gint
eject_all( gconstpointer data, gconstpointer user_data )
{
  const ui_media_drive_info_t *drive = data;

  return !drive_eject( drive );
}

int
ui_media_drive_eject_all( void )
{
  GSList *item;
  item = g_slist_find_custom( registered_drives, NULL, eject_all );
  return item ? 1 : 0;
}


int
ui_media_drive_insert( const ui_media_drive_info_t *drive,
                       const char *filename, int autoload )
{
  utils_file file;
  int error;

  if( !filename ) return ui_media_drive_insert_file( drive, NULL, autoload );

  utils_file_init( &file, filename );
  error = utils_file_read( &file ) ? 1 : 0;
  if( !error ) error = ui_media_drive_insert_file( drive, &file, autoload );
  utils_file_free( &file );
  return error;
}

static int
drive_install( const ui_media_drive_info_t *drive, const utils_file *file,
               int autoload )
{
  int error;
  const fdd_params_t *dt;
  const char *filename = file ? file->filename : NULL;

  if( filename ) {
    error = disk_open_loaded( &drive->fdd->disk, file, 0,
                              DISK_TRY_MERGE( drive->fdd->fdd_heads ) );
    if( error != DISK_OK ) {
      ui_error( UI_ERROR_ERROR, "Failed to open disk image: %s",
                disk_strerror( error ) );
      return 1;
    }
  } else {
    dt = drive->get_params();
    error = disk_new( &drive->fdd->disk, dt->heads, dt->cylinders, DISK_DENS_AUTO,
                      DISK_UDI );
    if( error != DISK_OK ) {
      ui_error( UI_ERROR_ERROR, "Failed to create disk image: %s",
                disk_strerror( error ) );
      return 1;
    }
  }
  if( drive->insert_hook ) {
    error = drive->insert_hook( drive, !filename );
    if( error )
      return 1;
  }

  fdd_load( drive->fdd, 0 );

  /* Set the 'eject' item active */
  ui_media_drive_update_menus( drive, UI_MEDIA_DRIVE_UPDATE_ALL );

  if( filename && autoload && drive->autoload_hook ) {
    return drive->autoload_hook();
  }

  return 0;
}

int
ui_media_drive_insert_file( const ui_media_drive_info_t *drive,
                            const utils_file *file, int autoload )
{
  /* Legacy cancellation or save failure also returns zero here. */
  if( drive->fdd->loaded && drive_eject( drive ) ) return 0;
  return drive_install( drive, file, autoload );
}

/* Own admission through completion here, including lookup/availability callbacks.
   A successful begin must be paired with completion in the operation entry. */
static fuse_host_operation_result
begin_disk_access( fuse_disk_controller controller, int slot,
                       const ui_media_drive_info_t **drive )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  if( controller < FUSE_DISK_CONTROLLER_PLUS3 || controller > FUSE_DISK_CONTROLLER_DIDAKTIK || slot < 0 )
    result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  else {
    *drive = ui_media_drive_find( controller, slot );
    if( !*drive ) result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  }
  if( result != FUSE_HOST_OPERATION_OK ) fuse_engine_end_operation();
  return result;
}

static fuse_host_operation_result
begin_disk_operation( fuse_disk_controller controller, int slot,
                       const ui_media_drive_info_t **drive )
{
  fuse_host_operation_result result = begin_disk_access( controller, slot, drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !(*drive)->is_available || !(*drive)->is_available() ||
      !(*drive)->fdd || (*drive)->fdd->type == FDD_TYPE_NONE ) {
    fuse_engine_end_operation();
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  }
  return FUSE_HOST_OPERATION_OK;
}

void
fuse_disk_drive_info_free( fuse_disk_drive_info *info )
{
  if( !info ) return;
  libspectrum_free( info->name );
  libspectrum_free( info->filename );
  libspectrum_free( info );
}

fuse_host_operation_result
fuse_host_copy_disk_drive_info( fuse_disk_controller controller, int slot,
                                fuse_disk_drive_info **info )
{
  const ui_media_drive_info_t *drive;
  fuse_disk_drive_info *copy;
  fuse_host_operation_result result;
  if( !info ) {
    result = fuse_engine_check_operation();
    return result == FUSE_HOST_OPERATION_OK ? FUSE_HOST_OPERATION_INVALID_ARGUMENT : result;
  }
  *info = NULL;
  result = begin_disk_access( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  copy = libspectrum_new0( fuse_disk_drive_info, 1 );
  copy->controller = controller;
  copy->slot = slot;
  copy->name = utils_safe_strdup( drive->name );
  copy->available = drive->is_available && !!drive->is_available();
  if( drive->fdd ) {
    copy->enabled = drive->fdd->type != FDD_TYPE_NONE;
    copy->loaded = !!drive->fdd->loaded;
    copy->flipped = !!drive->fdd->upsidedown;
    copy->write_protected = !!drive->fdd->wrprot;
    copy->filename = utils_safe_strdup( drive->fdd->disk.filename );
  }
  fuse_engine_end_operation();
  *info = copy;
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
request_disk_change( const ui_media_drive_info_t *drive, disk_change change,
                     utils_file *file, int autoload, int file_open,
                     fuse_media_decision_id *request )
{
  if( drive->fdd->loaded ) {
    if( drive->fdd->disk.dirty ) {
      *request = fuse_engine_request_decision( FUSE_DECISION_FLOPPY );
      pending_eject_drive = drive;
      pending_disk_change = change;
      pending_disk_autoload = autoload;
      pending_disk_file_open = file_open;
      if( file ) {
        pending_disk_file = *file;
        utils_file_init( file, NULL );
      }
      return FUSE_HOST_OPERATION_NEEDS_DECISION;
    }
    drive_finish_eject( drive );
  }
  if( change != DISK_CHANGE_EJECT && drive_install( drive, file, autoload ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return FUSE_HOST_OPERATION_OK;
}

int
ui_media_drive_request_open( const ui_media_drive_info_t *drive, utils_file *file,
                             int autoload, fuse_open_request *request )
{
  fuse_host_operation_result result = request_disk_change(
    drive, DISK_CHANGE_INSERT, file, autoload, 1, &request->id );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
    request->question = FUSE_OPEN_SAVE_FLOPPY;
    request->detail.floppy.controller = drive->controller_index;
    request->detail.floppy.slot = drive->drive_index;
    request->detail.floppy.has_save_target = drive->fdd->disk.filename != NULL;
  }
  return result != FUSE_HOST_OPERATION_OK && result != FUSE_HOST_OPERATION_NEEDS_DECISION;
}

static fuse_host_operation_result
host_replace_disk( fuse_disk_controller controller, int slot, disk_change change,
                   const char *filename, fuse_media_decision_id *request )
{
  const ui_media_drive_info_t *drive;
  fuse_host_operation_result result;

  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = 0;
  result = begin_disk_operation( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( change == DISK_CHANGE_INSERT && ( !filename || !*filename ) ) {
    result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  } else if( fuse_engine_pending_decision() != FUSE_DECISION_NONE ) {
    result = FUSE_HOST_OPERATION_BUSY;
  } else if( change == DISK_CHANGE_INSERT ) {
    utils_file file;
    utils_file_init( &file, filename );
    result = utils_file_read( &file ) ? FUSE_HOST_OPERATION_MEDIA_ERROR :
      request_disk_change( drive, change, &file, 0, 0, request );
    if( file.filename ) utils_file_free( &file );
  } else {
    result = request_disk_change( drive, change, NULL, 0, 0, request );
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_insert_disk_file( fuse_disk_controller controller, int slot,
                            const char *filename, fuse_media_decision_id *request )
{
  return host_replace_disk( controller, slot, DISK_CHANGE_INSERT, filename, request );
}

fuse_host_operation_result
fuse_host_save_disk_file( fuse_disk_controller controller, int slot,
                          const char *filename )
{
  const ui_media_drive_info_t *drive;
  fuse_host_operation_result result = begin_disk_operation( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( filename && !*filename ) result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  else if( !drive->fdd->loaded || ( !filename && !drive->fdd->disk.filename ) )
    result = FUSE_HOST_OPERATION_MEDIA_ERROR;
  else if( drive_save_authorized( drive, filename ) )
    result = FUSE_HOST_OPERATION_MEDIA_ERROR;
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_new_disk( fuse_disk_controller controller, int slot,
                    fuse_media_decision_id *request )
{
  return host_replace_disk( controller, slot, DISK_CHANGE_NEW, NULL, request );
}

fuse_host_operation_result
fuse_host_eject_disk( fuse_disk_controller controller, int slot,
                     fuse_media_decision_id *request )
{
  const ui_media_drive_info_t *drive;
  fuse_host_operation_result result;

  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = 0;
  result = begin_disk_operation( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;

  if( ( fuse_engine_pending_decision() != FUSE_DECISION_NONE &&
        fuse_engine_pending_decision() != FUSE_DECISION_FLOPPY ) ||
      ( pending_eject_drive && ( pending_eject_drive != drive ||
                                 pending_disk_change != DISK_CHANGE_EJECT ) ) ) {
    result = FUSE_HOST_OPERATION_BUSY;
  } else {
    result = request_disk_change( drive, DISK_CHANGE_EJECT, NULL, 0, 0, request );
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_disk_quit_next( fuse_quit_request *request )
{
  GSList *item;
  fuse_host_operation_result result = FUSE_HOST_OPERATION_OK;

  fuse_engine_begin_operation();
  for( item = registered_drives; item; item = item->next ) {
    const ui_media_drive_info_t *drive = item->data;
    if( !drive->fdd || !drive->fdd->loaded ) continue;
    result = request_disk_change( drive, DISK_CHANGE_EJECT, NULL, 0, 0, &request->id );
    if( result != FUSE_HOST_OPERATION_OK ) {
      if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
        pending_disk_quit = 1;
        request->floppy = 1;
        request->controller = drive->controller_index;
        request->slot = drive->drive_index;
      }
      break;
    }
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_answer_disk_eject( fuse_media_decision_id request,
                            fuse_media_decision_choice choice, const char *path )
{
  const ui_media_drive_info_t *drive;
  disk_change change;
  utils_file file;
  int autoload, file_open, quit;
  fuse_host_operation_result result = fuse_engine_check_operation();

  if( result != FUSE_HOST_OPERATION_OK ) return result;
  result = fuse_engine_validate_decision( FUSE_DECISION_FLOPPY, request,
                                          choice, path, 0 );
  if( result != FUSE_HOST_OPERATION_OK ) return result;

  fuse_engine_begin_operation();
  drive = pending_eject_drive;
  change = pending_disk_change;
  autoload = pending_disk_autoload;
  file_open = pending_disk_file_open;
  quit = pending_disk_quit;
  if( change == DISK_CHANGE_INSERT ) {
    file = pending_disk_file;
    pending_disk_change = DISK_CHANGE_EJECT;
    utils_file_init( &pending_disk_file, NULL );
  }
  retire_disk_eject();
  if( choice == FUSE_MEDIA_DECISION_CANCEL ) {
    result = FUSE_HOST_OPERATION_CANCELLED;
  } else if( !drive->fdd->loaded ||
             ( !quit && ( !drive->is_available || !drive->is_available() ||
                          drive->fdd->type == FDD_TYPE_NONE ) ) ) {
    result = FUSE_HOST_OPERATION_STALE;
  } else if( choice == FUSE_MEDIA_DECISION_SAVE && !path && !drive->fdd->disk.filename ) {
    result = FUSE_HOST_OPERATION_MEDIA_ERROR;
  } else {
    if( choice == FUSE_MEDIA_DECISION_SAVE && drive_save_authorized( drive, path ) )
      result = FUSE_HOST_OPERATION_MEDIA_ERROR;
    if( result == FUSE_HOST_OPERATION_OK ) {
      drive_finish_eject( drive );
      if( change != DISK_CHANGE_EJECT &&
          drive_install( drive, change == DISK_CHANGE_INSERT ? &file : NULL, autoload ) )
        result = FUSE_HOST_OPERATION_MEDIA_ERROR;
    }
  }
  if( change == DISK_CHANGE_INSERT ) utils_file_free( &file );
  if( file_open ) display_refresh_all();
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_set_disk_flipped( fuse_disk_controller controller, int slot, int flipped )
{
  int error;
  const ui_media_drive_info_t *drive;
  fuse_host_operation_result result = begin_disk_operation( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( flipped != 0 && flipped != 1 ) result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  else {
    error = ui_media_drive_flip( controller, slot, flipped );
    result = error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_set_disk_write_protected( fuse_disk_controller controller, int slot,
                                    int write_protected )
{
  int error;
  const ui_media_drive_info_t *drive;
  fuse_host_operation_result result = begin_disk_operation( controller, slot, &drive );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( write_protected != 0 && write_protected != 1 ) result = FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  else {
    error = ui_media_drive_writeprotect( controller, slot, write_protected );
    result = error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
  }
  fuse_engine_end_operation();
  return result;
}
