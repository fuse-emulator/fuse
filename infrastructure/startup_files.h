/* startup_files.h: private ownership of the legacy runner's startup inputs
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

#ifndef FUSE_STARTUP_FILES_H
#define FUSE_STARTUP_FILES_H

#include "utils.h"

typedef struct fuse_start_files {
  utils_file disk_plus3, disk_opus, disk_plusd, disk_beta, disk_didaktik80;
  utils_file disk_disciple, dock, if2, playback, snapshot, tape;
  utils_file mdr[8];

  /* Owned copies: insertion may replace the corresponding settings strings. */
  char *simpleide_master, *simpleide_slave;
  char *zxatasp_master, *zxatasp_slave;
  char *zxcf, *divide_master, *divide_slave, *divmmc, *zxmmc;

  /* Borrowed from settings until startup operations finish. */
  const char *recording;
} fuse_start_files;

/* Initialize an inactive record from current settings. This does not open files. */
void fuse_start_files_init( fuse_start_files *files );

/* Release even a zero-initialized/partially populated record; safe to repeat. */
void fuse_start_files_end( fuse_start_files *files );

#endif
