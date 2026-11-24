/* memory-filesystem.h: example platform filesystem for the direct host/tests
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
#ifndef FUSE_EXAMPLE_MEMORY_FILESYSTEM_H
#define FUSE_EXAMPLE_MEMORY_FILESYSTEM_H
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "fuse-filesystem.h"

typedef struct memory_file {
  char *name;
  unsigned char *data;
  size_t length;
  unsigned int handles;
  struct memory_file *next;
} memory_file;
typedef struct memory_handle {
  memory_file *file;
  size_t position;
  int mode;
} memory_handle;
typedef struct memory_filesystem {
  memory_file *files;
  size_t opens, closes, reads, writes, exists, unlinks, chunk;
  int fail_open, fail_read, fail_write, fail_close;
  const char *unreadable_name;
  int invalid_count, stall;
  int64_t fail_write_at;
  void (*on_read)(void);
} memory_filesystem;
static memory_filesystem *memory_backend;

static memory_file *
memory_find( const char *name )
{
  memory_file *file;
  for( file = memory_backend->files; file; file = file->next )
    if( !strcmp( file->name, name ) ) return file;
  return NULL;
}

static void *
memory_open( const char *name, int mode )
{
  memory_file *file;
  memory_handle *handle;
  if( memory_backend->fail_open ) { errno = EIO; return NULL; }
  if( ( mode == COMPAT_FILE_READ || mode == COMPAT_FILE_READ_WRITE ) &&
      memory_backend->unreadable_name && !strcmp( name, memory_backend->unreadable_name ) ) {
    errno = EACCES; return NULL;
  }
  file = memory_find( name );
  if( !file && ( mode == COMPAT_FILE_READ || mode == COMPAT_FILE_READ_WRITE ) ) {
    errno = ENOENT; return NULL;
  }
  if( !file ) {
    file = calloc( 1, sizeof(*file) );
    if( !file ) return NULL;
    file->name = malloc( strlen(name) + 1 );
    if( !file->name ) { free( file ); return NULL; }
    strcpy( file->name, name );
    file->next = memory_backend->files; memory_backend->files = file;
  }
  handle = calloc( 1, sizeof(*handle) );
  if( !handle ) return NULL;
  if( mode == COMPAT_FILE_WRITE ) { free( file->data ); file->data = NULL; file->length = 0; }
  handle->file = file; handle->mode = mode;
  if( mode == COMPAT_FILE_APPEND ) handle->position = file->length;
  file->handles++; memory_backend->opens++;
  return handle;
}

static int64_t
memory_length( void * opaque )
{
  memory_handle *handle = opaque;
  return (int64_t)handle->file->length;
}

static int
memory_read( void * opaque, unsigned char *buffer, size_t length, size_t *got )
{
  memory_handle *handle = opaque;
  size_t left = handle->position < handle->file->length ? handle->file->length - handle->position : 0;
  *got = 0; memory_backend->reads++;
  if( memory_backend->on_read ) memory_backend->on_read();
  if( memory_backend->invalid_count ) { *got = length + 1; return 0; }
  if( handle->mode == COMPAT_FILE_WRITE || handle->mode == COMPAT_FILE_APPEND || memory_backend->fail_read ) {
    errno = EIO; return 1;
  }
  if( length > left ) length = left;
  if( memory_backend->chunk && length > memory_backend->chunk ) length = memory_backend->chunk;
  if( length ) memcpy( buffer, handle->file->data + handle->position, length );
  handle->position += length; *got = length;
  return 0;
}

static int
memory_write( void * opaque, const unsigned char *buffer, size_t length, size_t *put )
{
  memory_handle *handle = opaque;
  size_t end;
  unsigned char *data;
  *put = 0; memory_backend->writes++;
  if( memory_backend->stall ) return 0;
  if( memory_backend->fail_write_at > 0 &&
      handle->position >= (uint64_t)memory_backend->fail_write_at ) return 1;
  if( handle->mode == COMPAT_FILE_READ || memory_backend->fail_write ) { errno = EIO; return 1; }
  if( memory_backend->chunk && length > memory_backend->chunk ) length = memory_backend->chunk;
  if( handle->mode == COMPAT_FILE_APPEND ) handle->position = handle->file->length;
  if( length > SIZE_MAX - handle->position ) return 1;
  end = handle->position + length;
  if( end > handle->file->length ) {
    data = realloc( handle->file->data, end );
    if( !data ) return 1;
    memset( data + handle->file->length, 0, end - handle->file->length );
    handle->file->data = data; handle->file->length = end;
  }
  if( length ) memcpy( handle->file->data + handle->position, buffer, length );
  handle->position = end; *put = length;
  return 0;
}

static int
memory_close( void * opaque )
{
  memory_handle *handle = opaque;
  handle->file->handles--; free( handle ); memory_backend->closes++;
  return memory_backend->fail_close ? 1 : 0;
}

static int
memory_exists( const char *name )
{
  memory_backend->exists++; return memory_find( name ) != NULL;
}

static int
memory_unlink( const char *name )
{
  memory_file **file = &memory_backend->files, *old;
  memory_backend->unlinks++;
  while( *file && strcmp( (*file)->name, name ) ) file = &(*file)->next;
  if( !*file || (*file)->handles ) return 1;
  old = *file; *file = old->next;
  free( old->name ); free( old->data ); free( old );
  return 0;
}

static int
memory_seek( void * opaque, int64_t offset, int origin )
{
  memory_handle *handle = opaque;
  int64_t base, pos;
  switch( origin ) {
  case COMPAT_SEEK_SET: base = 0; break;
  case COMPAT_SEEK_CUR: base = handle->position; break;
  case COMPAT_SEEK_END: base = handle->file->length; break;
  default: return 1;
  }
  if( ( offset > 0 && base > INT64_MAX - offset ) || ( offset < 0 && offset < -base ) ) return 1;
  pos = base + offset;
  if( pos < 0 || (uint64_t)pos > SIZE_MAX ) return 1;
  handle->position = (size_t)pos;
  return 0;
}
static int64_t memory_tell( void * opaque ) { return ((memory_handle *)opaque)->position; }
static int memory_flush( void * opaque ) { (void)opaque; return 0; }
static int memory_writable( const char *name ) { return memory_find( name ) != NULL; }

#define MEM_RESULT(error) ((error) ? LIBSPECTRUM_ERROR_UNKNOWN : LIBSPECTRUM_ERROR_NONE)
static libspectrum_error mem_open(void *ctx, const char *name, libspectrum_fs_mode mode, void **out)
{ (void)ctx; *out = memory_open(name, mode); return MEM_RESULT(!*out); }
static libspectrum_error mem_length(void *ctx, void *h, int64_t *out)
{ (void)ctx; *out = memory_length(h); return MEM_RESULT(*out < 0); }
static libspectrum_error mem_read(void *ctx, void *h, void *buf, size_t n, size_t *out)
{ (void)ctx; return MEM_RESULT(memory_read(h, buf, n, out)); }
static libspectrum_error mem_write(void *ctx, void *h, const void *buf, size_t n, size_t *out)
{ (void)ctx; return MEM_RESULT(memory_write(h, buf, n, out)); }
static libspectrum_error mem_seek(void *ctx, void *h, int64_t offset, libspectrum_fs_origin origin)
{ (void)ctx; return MEM_RESULT(memory_seek(h, offset, origin)); }
static libspectrum_error mem_tell(void *ctx, void *h, int64_t *out)
{ (void)ctx; *out = memory_tell(h); return MEM_RESULT(*out < 0); }
static libspectrum_error mem_flush(void *ctx, void *h)
{ (void)ctx; return MEM_RESULT(memory_flush(h)); }
static libspectrum_error mem_close(void *ctx, void *h)
{ (void)ctx; return MEM_RESULT(memory_close(h)); }
static libspectrum_error mem_exists(void *ctx, const char *name, int *out)
{ (void)ctx; *out = memory_exists(name); return LIBSPECTRUM_ERROR_NONE; }
static libspectrum_error mem_unlink(void *ctx, const char *name)
{ (void)ctx; return MEM_RESULT(memory_unlink(name)); }
static libspectrum_error mem_writable(void *ctx, const char *name, int *out)
{ (void)ctx; *out = memory_writable(name); return MEM_RESULT(!*out); }
#undef MEM_RESULT

static int
memory_filesystem_install( memory_filesystem *filesystem )
{
  const libspectrum_fs_backend backend = { mem_open, mem_read, mem_write,
    mem_length, mem_seek, mem_tell, mem_flush, mem_close, mem_exists,
    mem_unlink, mem_writable };
  if( libspectrum_fs_install( &backend, filesystem ) ) return 1;
  memory_backend = filesystem;
  return 0;
}

static void
memory_filesystem_free( memory_filesystem *filesystem )
{
  memory_file *file = filesystem->files;
  while( file ) {
    memory_file *next = file->next;
    free( file->name ); free( file->data ); free( file ); file = next;
  }
  filesystem->files = NULL;
  if( memory_backend == filesystem ) memory_backend = NULL;
}
#endif /* FUSE_EXAMPLE_MEMORY_FILESYSTEM_H */
