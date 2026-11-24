/* uimedia.h: Disk media UI routines
   Copyright (c) 2013 Alex Badea
   Copyright (c) 2015 Gergely Szasz

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

#ifndef FUSE_UIMEDIA_H
#define FUSE_UIMEDIA_H

#include "peripherals/disk/drive.h"
#include "ui/ui.h"

#define UI_MEDIA_DRIVE_UPDATE_ALL	(~0)
#define UI_MEDIA_DRIVE_UPDATE_TOP	(1 << 0)
#define UI_MEDIA_DRIVE_UPDATE_EJECT	(1 << 1)
#define UI_MEDIA_DRIVE_UPDATE_FLIP	(1 << 2)
#define UI_MEDIA_DRIVE_UPDATE_WP	(1 << 3)

void ui_media_drive_update_parent_menus( void );
void ui_media_drive_update_menus( const disk_drive_info_t *drive,
                                  unsigned flags );
char *ui_media_choose_save_path( const char *name );
int ui_media_confirm_overwrite( const char *filename );
ui_confirm_save_t ui_media_confirm_dirty( const char *name );

#endif
