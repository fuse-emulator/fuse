/* startup_files.c: own temporary startup files independently of settings
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

#include "settings.h"
#include "startup_files.h"

void
fuse_start_files_init( fuse_start_files *files )
{
  memset( files, 0, sizeof( *files ) );

  utils_file_init( &files->disk_plus3, settings_current.plus3disk_file );
  utils_file_init( &files->disk_opus, settings_current.opusdisk_file );
  utils_file_init( &files->disk_plusd, settings_current.plusddisk_file );
  utils_file_init( &files->disk_didaktik80, settings_current.didaktik80disk_file );
  utils_file_init( &files->disk_disciple, settings_current.discipledisk_file );
  utils_file_init( &files->disk_beta, settings_current.betadisk_file );
  utils_file_init( &files->dock, settings_current.dck_file );
  utils_file_init( &files->if2, settings_current.if2_file );
  utils_file_init( &files->playback, settings_current.playback_file );
  utils_file_init( &files->snapshot, settings_current.snapshot );
  utils_file_init( &files->tape, settings_current.tape_file );

  files->simpleide_master = utils_safe_strdup( settings_current.simpleide_master_file );
  files->simpleide_slave = utils_safe_strdup( settings_current.simpleide_slave_file );
  files->zxatasp_master = utils_safe_strdup( settings_current.zxatasp_master_file );
  files->zxatasp_slave = utils_safe_strdup( settings_current.zxatasp_slave_file );
  files->zxcf = utils_safe_strdup( settings_current.zxcf_pri_file );
  files->divide_master = utils_safe_strdup( settings_current.divide_master_file );
  files->divide_slave = utils_safe_strdup( settings_current.divide_slave_file );
  files->divmmc = utils_safe_strdup( settings_current.divmmc_file );
  files->zxmmc = utils_safe_strdup( settings_current.zxmmc_file );

  utils_file_init( &files->mdr[0], settings_current.mdr_file );
  utils_file_init( &files->mdr[1], settings_current.mdr_file2 );
  utils_file_init( &files->mdr[2], settings_current.mdr_file3 );
  utils_file_init( &files->mdr[3], settings_current.mdr_file4 );
  utils_file_init( &files->mdr[4], settings_current.mdr_file5 );
  utils_file_init( &files->mdr[5], settings_current.mdr_file6 );
  utils_file_init( &files->mdr[6], settings_current.mdr_file7 );
  utils_file_init( &files->mdr[7], settings_current.mdr_file8 );

  files->recording = settings_current.record_file;
}

void
fuse_start_files_end( fuse_start_files *files )
{
  int i;

  utils_file_free( &files->disk_plus3 );
  utils_file_free( &files->disk_opus );
  utils_file_free( &files->disk_plusd );
  utils_file_free( &files->disk_beta );
  utils_file_free( &files->disk_didaktik80 );
  utils_file_free( &files->disk_disciple );
  utils_file_free( &files->dock );
  utils_file_free( &files->if2 );
  utils_file_free( &files->playback );
  utils_file_free( &files->snapshot );
  utils_file_free( &files->tape );
  for( i = 0; i < 8; i++ ) utils_file_free( &files->mdr[i] );

  libspectrum_free( files->simpleide_master );
  libspectrum_free( files->simpleide_slave );
  libspectrum_free( files->zxatasp_master );
  libspectrum_free( files->zxatasp_slave );
  libspectrum_free( files->zxcf );
  libspectrum_free( files->divide_master );
  libspectrum_free( files->divide_slave );
  libspectrum_free( files->divmmc );
  libspectrum_free( files->zxmmc );
  memset( files, 0, sizeof( *files ) );
}
