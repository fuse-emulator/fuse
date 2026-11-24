/* fuse-filesystem.h: replaceable effective filesystem for Fuse
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef FUSE_FILESYSTEM_H
#define FUSE_FILESYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include <libspectrum.h>

typedef libspectrum_fs_handle *compat_fd;
extern const compat_fd COMPAT_FILE_OPEN_FAILED;
#define COMPAT_FILE_READ LIBSPECTRUM_FS_READ
#define COMPAT_FILE_WRITE LIBSPECTRUM_FS_WRITE
#define COMPAT_FILE_READ_WRITE LIBSPECTRUM_FS_READ_WRITE
#define COMPAT_FILE_APPEND LIBSPECTRUM_FS_APPEND
#define COMPAT_SEEK_SET LIBSPECTRUM_FS_START
#define COMPAT_SEEK_CUR LIBSPECTRUM_FS_CURRENT
#define COMPAT_SEEK_END LIBSPECTRUM_FS_END

/* Null-build legacy fault interception; not a filesystem backend. */
#ifdef ENABLE_INTERNAL_TESTS
typedef struct compat_file_test_hooks {
  compat_fd (*open)( const char *name, int mode );
  int64_t (*get_length)( compat_fd file );
  int (*read)( compat_fd file, unsigned char *buffer, size_t length, size_t *transferred );
  int (*write)( compat_fd file, const unsigned char *buffer, size_t length, size_t *transferred );
  int (*close)( compat_fd file );
  int (*exists)( const char *name );
  int (*unlink)( const char *name );
  int (*seek)( compat_fd file, int64_t offset, int origin );
  int64_t (*tell)( compat_fd file );
  int (*flush)( compat_fd file );
  int (*writable)( const char *name ); /* Existing file's write permission,0/1. */
} compat_file_test_hooks;

int compat_file_test_set_hooks( const compat_file_test_hooks *vtable );
void compat_file_test_get_hooks( compat_file_test_hooks *vtable );
#endif

compat_fd compat_file_open( const char *name, int mode );
int64_t compat_file_get_length( compat_fd file );
int compat_file_read_some( compat_fd file, unsigned char *buffer, size_t length, size_t *transferred );
int compat_file_write_some( compat_fd file, const unsigned char *buffer, size_t length, size_t *transferred );
/* Exact helpers loop successful short transfers; EOF/no progress is failure. */
int compat_file_read( compat_fd file, unsigned char *buffer, size_t length );
int compat_file_write( compat_fd file, const unsigned char *buffer, size_t length );
int compat_file_close( compat_fd file );
int compat_file_exists( const char *name );
int compat_file_unlink( const char *name );
int compat_file_seek( compat_fd file, int64_t offset, int origin );
int64_t compat_file_tell( compat_fd file );
int compat_file_flush( compat_fd file );
int compat_file_writable( const char *name );
#endif /* FUSE_FILESYSTEM_H */
