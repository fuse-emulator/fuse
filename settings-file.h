/* settings-file.h: optional Fuse settings file codec
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

#ifndef FUSE_SETTINGS_FILE_H
#define FUSE_SETTINGS_FILE_H

#include "settings.h"

/* Explicit paths in the effective filesystem. Load overlays an initialized,
   owned record and leaves it unchanged on failure. Zero means success.
   Save may replace the destination; it is not an atomic write. */
int settings_file_load( const char *path, settings_info *settings );
int settings_file_save( const char *path, const settings_info *settings );

#endif
