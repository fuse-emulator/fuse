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

#ifndef FUSE_DISK_DRIVE_H
#define FUSE_DISK_DRIVE_H

#include "fuse-disk.h"
#include "utils.h"

struct fdd_t;
struct fdd_params_t;
struct fuse_open_request;
struct disk_drive_info_t;

typedef int (*disk_drive_is_available_fn)( void );
typedef const struct fdd_params_t* (*disk_drive_get_params_fn) ( void );
typedef int (*disk_drive_insert_hook_fn)(
  const struct disk_drive_info_t *drive, int new );
typedef int (*disk_drive_autoload_hook_fn)( void );

typedef struct disk_drive_info_t
{
  const char *name;
  fuse_disk_controller controller_index;
  int drive_index;
  int menu_item_parent;
  int menu_item_top;
  int menu_item_eject;
  int menu_item_flip;
  int menu_item_wp;
  disk_drive_is_available_fn is_available;
  disk_drive_get_params_fn get_params;
  disk_drive_insert_hook_fn insert_hook;
  disk_drive_autoload_hook_fn autoload_hook;
  struct fdd_t *fdd;
} disk_drive_info_t;

int disk_drive_register( disk_drive_info_t *drive );
void disk_drive_end( void );
disk_drive_info_t *disk_drive_find( fuse_disk_controller controller, int drive );

void disk_drive_foreach( void (*visit)( const disk_drive_info_t *drive ) );
int disk_drive_any_available( void );
int disk_drive_eject_all( void );

int disk_drive_insert( const disk_drive_info_t *drive,
                           const char *filename, int autoload );
int disk_drive_insert_file( const disk_drive_info_t *drive,
                                const utils_file *file, int autoload );
int disk_drive_save( fuse_disk_controller controller, int which, int saveas );
int disk_drive_eject( fuse_disk_controller controller, int which );
int disk_drive_flip( fuse_disk_controller controller, int which, int flip );
int disk_drive_writeprotect( fuse_disk_controller controller, int which, int wrprot );

int disk_drive_request_open( const disk_drive_info_t *drive,
                                 utils_file *file, int autoload,
                                 struct fuse_open_request *request );

#endif
