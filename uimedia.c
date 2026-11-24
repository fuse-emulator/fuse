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
#include <stdio.h>
#include <string.h>

#include "compat.h"
#include "peripherals/disk/fdd.h"
#include "ui/uimedia.h"

static inline int
menu_item_valid( ui_menu_item item )
{
  return item != UI_MENU_ITEM_INVALID;
}

static void
update_parent_menus( const disk_drive_info_t *drive )
{
  if( drive->is_available && menu_item_valid( drive->menu_item_parent ) )
    ui_menu_activate( drive->menu_item_parent, drive->is_available() );
}

void
ui_media_drive_update_parent_menus( void )
{
  disk_drive_foreach( update_parent_menus );
}

static int
maybe_menu_activate( int id, int activate )
{
  if( !menu_item_valid( id ) )
    return 0;
  return ui_menu_activate( id, activate );
}

void
ui_media_drive_update_menus( const disk_drive_info_t *drive,
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


char *
ui_media_choose_save_path( const char *name )
{
  char title[80];
  snprintf( title, sizeof( title ), "Fuse - Write %s", name );
  return ui_get_save_filename( title );
}

int
ui_media_confirm_overwrite( const char *filename )
{
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
  return 0;
}

ui_confirm_save_t
ui_media_confirm_dirty( const char *name )
{
  return ui_confirm_save( "%s has been modified.\nDo you want to save it?",
                          name );
}
