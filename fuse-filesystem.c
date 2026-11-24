/* fuse-filesystem.c: Fuse adapters for the libspectrum filesystem
   Copyright (c) 2008-2011 Philip Kendall
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

   Author contact information:
   E-mail: philip-fuse@shadowmagic.org.uk
*/
#include "config.h"
#include "fuse-filesystem.h"

const compat_fd COMPAT_FILE_OPEN_FAILED = NULL;

static compat_fd
open_file( const char *name, int mode )
{
  compat_fd file;
  return libspectrum_fs.open( name, mode, &file ) ? NULL : file;
}

static int64_t
length_file( compat_fd file )
{
  int64_t result;
  return libspectrum_fs.length( file, &result ) ? -1 : result;
}

static int
read_file( compat_fd file, unsigned char *buf, size_t size, size_t *got )
{
  return libspectrum_fs.read( file, buf, size, got );
}

static int
write_file( compat_fd file, const unsigned char *buf, size_t size, size_t *put )
{
  return libspectrum_fs.write( file, buf, size, put );
}

static int
close_file( compat_fd file )
{
  return libspectrum_fs.close( file );
}

static int
exists_file( const char *name )
{
  int result;
  return !libspectrum_fs.exists( name, &result ) && result;
}

static int
unlink_file( const char *name )
{
  return libspectrum_fs.unlink( name );
}

int
compat_file_seek( compat_fd file, int64_t offset, int origin )
{
  return libspectrum_fs.seek( file, offset, origin );
}

int64_t
compat_file_tell( compat_fd file )
{
  int64_t result;
  return libspectrum_fs.tell( file, &result ) ? -1 : result;
}

int
compat_file_flush( compat_fd file )
{
  return libspectrum_fs.flush( file );
}

int
compat_file_writable( const char *name )
{
  int result;
  return !libspectrum_fs.writable( name, &result ) && result;
}

#ifdef ENABLE_INTERNAL_TESTS
/* Legacy unit-test interception at the Fuse call site. This does not select,
   replace, or forward a libspectrum backend. */
static const compat_file_test_hooks defaults = {
  open_file, length_file, read_file, write_file, close_file, exists_file,
  unlink_file, compat_file_seek, compat_file_tell, compat_file_flush,
  compat_file_writable
};

static compat_file_test_hooks hooks;

static int intercept;

int
compat_file_test_set_hooks( const compat_file_test_hooks *table )
{
  if( table ) hooks = *table;
  intercept = table && ( table->open != defaults.open ||
                         table->get_length != defaults.get_length ||
                         table->read != defaults.read ||
                         table->write != defaults.write ||
                         table->close != defaults.close ||
                         table->exists != defaults.exists ||
                         table->unlink != defaults.unlink );
  return 0;
}

void
compat_file_test_get_hooks( compat_file_test_hooks *table )
{
  *table = intercept ? hooks : defaults;
}

#define CALL( method, fallback, \
              args ) ( intercept ? hooks.method args : fallback args )
#else
#define CALL( method, fallback, args ) fallback args
#endif

compat_fd
compat_file_open( const char *name, int mode )
{
  return CALL( open, open_file, ( name, mode ) );
}

int64_t
compat_file_get_length( compat_fd file )
{
  return CALL( get_length, length_file, ( file ) );
}

int
compat_file_read_some( compat_fd file, unsigned char *buf, size_t size,
                       size_t *got )
{
  return CALL( read, read_file, ( file, buf, size, got ) );
}

int
compat_file_write_some( compat_fd file, const unsigned char *buf, size_t size,
                        size_t *put )
{
  return CALL( write, write_file, ( file, buf, size, put ) );
}

int
compat_file_close( compat_fd file )
{
  return CALL( close, close_file, ( file ) );
}

int
compat_file_exists( const char *name )
{
  return CALL( exists, exists_file, ( name ) );
}

int
compat_file_unlink( const char *name )
{
  return CALL( unlink, unlink_file, ( name ) );
}

int
compat_file_read( compat_fd file, unsigned char *buffer, size_t length )
{
#ifdef ENABLE_INTERNAL_TESTS
  if( intercept ) {
    while( length ) {
      size_t got = 0;
      if( compat_file_read_some( file, buffer, length,
                                 &got ) || !got || got > length ) return 1;
      buffer += got; length -= got;
    }
    return 0;
  }
#endif
  return libspectrum_fs.read_exact( file, buffer, length );
}

int
compat_file_write( compat_fd file, unsigned char const *buffer, size_t length )
{
#ifdef ENABLE_INTERNAL_TESTS
  if( intercept ) {
    while( length ) {
      size_t put = 0;
      if( compat_file_write_some( file, buffer, length,
                                  &put ) || !put || put > length ) return 1;
      buffer += put; length -= put;
    }
    return 0;
  }
#endif
  return libspectrum_fs.write_exact( file, buffer, length );
}
