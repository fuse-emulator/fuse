/* utils.c: some useful helper functions
   Copyright (c) 1999-2018 Philip Kendall
   Copyright (c) 2015 Stuart Brady
   Copyright (c) 2015 Gergely Szasz
   Copyright (c) 2015-2021 Fredrick Meunier
   Copyright (c) 2016 BogDan Vatra
   Copyright (c) 2016-2017 Sergio Baldoví

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

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#include <errno.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif				/* #ifdef HAVE_LIBGEN_H */
#include <string.h>
#include <ui/ui.h>
#include <unistd.h>

#include "libspectrum.h"

#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif
#include "fuse.h"
#include "fuse-engine.h"
#include "machines/specplus3.h"
#include "memory_pages.h"
#include "peripherals/dck.h"
#include "peripherals/ide/divide.h"
#include "peripherals/ide/divmmc.h"
#include "peripherals/ide/simpleide.h"
#include "peripherals/ide/zxatasp.h"
#include "peripherals/ide/zxcf.h"
#include "peripherals/ide/zxmmc.h"
#include "peripherals/if1.h"
#include "peripherals/if2.h"
#include "pokefinder/pokemem.h"
#include "rzx.h"
#include "screenshot.h"
#include "settings.h"
#include "snapshot.h"
#include "tape.h"
#include "tape_internals.h"
#include "utils.h"
#include "ui/uimedia.h"

static void init_path_context( path_context *ctx, utils_aux_type type );

#define UTILS_IDENTIFY_LENGTH 64

static int networking_init_count = 0;

libspectrum_word
utils_read_le16( const libspectrum_byte *buffer )
{
  return buffer[0] | ( buffer[1] << 8 );
}

#ifdef ENABLE_AUTOMATION
static const char *
automation_disk_controller_name( fuse_disk_controller controller )
{
  switch( controller ) {
  case FUSE_DISK_CONTROLLER_PLUS3: return "plus3";
  case FUSE_DISK_CONTROLLER_BETA: return "beta";
  case FUSE_DISK_CONTROLLER_PLUSD: return "plusd";
  case FUSE_DISK_CONTROLLER_OPUS: return "opus";
  case FUSE_DISK_CONTROLLER_DISCIPLE: return "disciple";
  case FUSE_DISK_CONTROLLER_DIDAKTIK: return "didaktik";
  default: return "unknown";
  }
}
#endif

static int
utils_insert_disk( fuse_disk_controller controller, utils_file *file,
                   int autoload, fuse_open_request *request )
{
  ui_media_drive_info_t *drive = ui_media_drive_find( controller, 0 );
#ifdef ENABLE_AUTOMATION
  /* Disk insertion may normalise the loaded image in place, so retain the
     source identity before handing the buffer to the drive. */
  if( drive && automation_active() )
    automation_record_disk( file,
                            automation_disk_controller_name( controller ), 0 );
#endif
  if( !drive ) return 1;
  return request ? ui_media_drive_request_open( drive, file, autoload, request ) :
                   ui_media_drive_insert_file( drive, file, autoload );
}

/* Open `filename' and do something sensible with it; autoload tapes
   if `autoload' is true and return the type of file found in `type' */
int
utils_open_file( const char *filename, int autoload,
                 libspectrum_id_t *type_ptr)
{
  utils_file file;
  int error;

  utils_file_init( &file, filename );
  error = utils_open_loaded_file( &file, autoload, type_ptr );
  utils_file_free( &file );
  return error;
}

static int
open_loaded_file( utils_file *file, int autoload, libspectrum_id_t *type_ptr,
                  fuse_open_request *request )
{
  int error;

  error = 0;
  if( rzx_recording ) error = rzx_stop_recording();
  if( rzx_playback  ) error = rzx_stop_playback( 1 );
  if( error ) return error;

  if( utils_file_identify( file ) ) return 1;
#ifdef ENABLE_AUTOMATION
  if( automation_active() ) {
    automation_record_tape( file );
    if( libspectrum_file_class( file ) == LIBSPECTRUM_CLASS_RECORDING )
      automation_record_rzx( file );
    else if( libspectrum_file_class( file ) == LIBSPECTRUM_CLASS_SNAPSHOT )
      automation_record_external_snapshot( file );
  }
#endif

  /* Keep the existing dispatch readable while the loaded file owns the data. */
  {
    const char *filename = file->filename;
    libspectrum_id_t type = file->type;

  switch( file->file_class ) {
    
  case LIBSPECTRUM_CLASS_UNKNOWN:
    ui_error( UI_ERROR_ERROR, "utils_open_file: couldn't identify `%s'",
	      file->filename );
    return 1;

  case LIBSPECTRUM_CLASS_RECORDING:
    error = request ? rzx_request_open( file, NULL, request ) :
      rzx_start_playback_from_buffer( file->buffer, file->length );
    break;

  case LIBSPECTRUM_CLASS_SNAPSHOT:
    error = request ? snapshot_request_open( file, request ) :
      snapshot_read_buffer( file->buffer, file->length, file->type );
    if( request && request->id ) return error;
    pokemem_find_pokfile( file->filename );
    break;

  case LIBSPECTRUM_CLASS_TAPE:
    if( request ) {
      error = tape_request_insert( file, filename, type, autoload, 1,
                                   &request->id );
      if( request->id ) {
        request->question = FUSE_OPEN_SAVE_TAPE;
        return error;
      }
    } else {
      error = tape_read_buffer( file->buffer, file->length, type, filename, autoload );
    }
    pokemem_find_pokfile( file->filename );
    break;

  case LIBSPECTRUM_CLASS_DISK_PLUS3:
    if( !( machine_current->capabilities &
	   LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_DISK ) ) {
      error = machine_select( LIBSPECTRUM_MACHINE_PLUS3 ); if( error ) break;
    }

    error = utils_insert_disk( FUSE_DISK_CONTROLLER_PLUS3, file, autoload, request );
    break;

  case LIBSPECTRUM_CLASS_DISK_DIDAKTIK:

    error = utils_insert_disk( FUSE_DISK_CONTROLLER_DIDAKTIK, file, autoload, request );
    break;

  case LIBSPECTRUM_CLASS_DISK_PLUSD:

    if( periph_is_active( PERIPH_TYPE_DISCIPLE ) )
      error = utils_insert_disk( FUSE_DISK_CONTROLLER_DISCIPLE, file, autoload, request );
    else
      error = utils_insert_disk( FUSE_DISK_CONTROLLER_PLUSD, file, autoload, request );
    break;

  case LIBSPECTRUM_CLASS_DISK_OPUS:

    error = utils_insert_disk( FUSE_DISK_CONTROLLER_OPUS, file, autoload, request );
    break;

  case LIBSPECTRUM_CLASS_DISK_TRDOS:

    if( !( machine_current->capabilities &
	   LIBSPECTRUM_MACHINE_CAPABILITY_TRDOS_DISK ) &&
        !periph_is_active( PERIPH_TYPE_BETA128 ) ) {
      error = machine_select( LIBSPECTRUM_MACHINE_SCORP ); if( error ) break;
    }

    /* Check that we actually got a Beta capable machine to insert the disk */
    if( ( machine_current->capabilities & 
          LIBSPECTRUM_MACHINE_CAPABILITY_TRDOS_DISK ) ||
        periph_is_active( PERIPH_TYPE_BETA128 ) ) {
      error = utils_insert_disk( FUSE_DISK_CONTROLLER_BETA, file, autoload, request );
    }
    break;

  case LIBSPECTRUM_CLASS_DISK_GENERIC:
    if( machine_current->machine == LIBSPECTRUM_MACHINE_PLUS3 ||
        machine_current->machine == LIBSPECTRUM_MACHINE_PLUS2A )
      error = utils_insert_disk( FUSE_DISK_CONTROLLER_PLUS3, file, autoload, request );
    else if( machine_current->machine == LIBSPECTRUM_MACHINE_PENT ||
          machine_current->machine == LIBSPECTRUM_MACHINE_PENT512 ||
          machine_current->machine == LIBSPECTRUM_MACHINE_PENT1024 ||
          machine_current->machine == LIBSPECTRUM_MACHINE_SCORP )
      error = utils_insert_disk( FUSE_DISK_CONTROLLER_BETA, file, autoload, request );
    else
      if( periph_is_active( PERIPH_TYPE_BETA128 ) )
        error = utils_insert_disk( FUSE_DISK_CONTROLLER_BETA, file, autoload, request );
      else if( periph_is_active( PERIPH_TYPE_DISCIPLE ) )
        error = utils_insert_disk( FUSE_DISK_CONTROLLER_DISCIPLE, file, autoload, request );
      else if( periph_is_active( PERIPH_TYPE_PLUSD ) )
        error = utils_insert_disk( FUSE_DISK_CONTROLLER_PLUSD, file, autoload, request );
    break;

  case LIBSPECTRUM_CLASS_CARTRIDGE_IF2:
    error = if2_insert_loaded( file );
    break;

  case LIBSPECTRUM_CLASS_MICRODRIVE:
    error = if1_mdr_insert_loaded( -1, file );
    break;

  case LIBSPECTRUM_CLASS_CARTRIDGE_TIMEX:
    if( !( machine_current->capabilities &
	   LIBSPECTRUM_MACHINE_CAPABILITY_TIMEX_DOCK ) ) {
      error = machine_select( LIBSPECTRUM_MACHINE_TC2068 ); if( error ) break;
    }
    /* Check that we actually got a Dock capable machine to insert the cart */
    if( machine_current->capabilities &
	   LIBSPECTRUM_MACHINE_CAPABILITY_TIMEX_DOCK ) {
      error = dck_insert_loaded( file );
    }
    break;

  case LIBSPECTRUM_CLASS_HARDDISK: {
    periph_type controller;
    if( !settings_current.simpleide_active &&
	!settings_current.zxatasp_active   &&
	!settings_current.divide_enabled   &&
	!settings_current.divmmc_enabled   &&
	!settings_current.zxmmc_enabled    &&
	!settings_current.zxcf_active         ) {
      settings_current.zxcf_active = 1;
      periph_update();
    }

    if( settings_current.zxcf_active ) controller = PERIPH_TYPE_ZXCF;
    else if( settings_current.zxatasp_active ) controller = PERIPH_TYPE_ZXATASP;
    else if( settings_current.simpleide_active ) controller = PERIPH_TYPE_SIMPLEIDE;
    else if( settings_current.divide_enabled ) controller = PERIPH_TYPE_DIVIDE;
    else if( settings_current.zxmmc_enabled ) controller = PERIPH_TYPE_ZXMMC;
    else controller = PERIPH_TYPE_DIVMMC;

    if( request ) return fuse_mass_storage_request_open( controller, filename, request );
    switch( controller ) {
    case PERIPH_TYPE_ZXCF: error = zxcf_insert( filename ); break;
    case PERIPH_TYPE_ZXATASP: error = zxatasp_insert( filename, LIBSPECTRUM_IDE_MASTER ); break;
    case PERIPH_TYPE_SIMPLEIDE: error = simpleide_insert( filename, LIBSPECTRUM_IDE_MASTER ); break;
    case PERIPH_TYPE_DIVIDE: error = divide_insert( filename, LIBSPECTRUM_IDE_MASTER ); break;
    case PERIPH_TYPE_ZXMMC: error = zxmmc_insert( filename ); break;
    default: error = divmmc_insert( filename ); break;
    }
    break;
  }

  case LIBSPECTRUM_CLASS_AUXILIARY:
    if( type == LIBSPECTRUM_ID_AUX_POK ) {
      ui_pokemem_selector( filename );
    }
    break;

  default:
    ui_error( UI_ERROR_ERROR, "utils_open_file: unknown class %d", type );
    error = 1;
    break;
  }

  if( error ) return error;

  if( type_ptr ) *type_ptr = file->type;

  return 0;
  }
}

int
utils_open_loaded_file( utils_file *file, int autoload, libspectrum_id_t *type )
{
  return open_loaded_file( file, autoload, type, NULL );
}

int
utils_open_file_with_decision( const char *filename, int autoload,
                               fuse_open_request *request )
{
  utils_file file;
  int error;

  utils_file_init( &file, filename );
  error = open_loaded_file( &file, autoload, NULL, request );
  utils_file_free( &file );
  return error;
}

/* Request a snapshot file from the user and it */
int
utils_open_snap( void )
{
  char *filename;
  int error;

  filename = ui_get_open_filename( "Fuse - Load Snapshot" );
  if( !filename ) return -1;

  error = snapshot_read( filename );
  libspectrum_free( filename );
  return error;
}

static compat_fd find_readable_path( const char *, char *, utils_aux_type );

int
utils_file_is_readable( const char *name )
{
  compat_fd fd = compat_file_open( name, COMPAT_FILE_READ );
  if( fd == COMPAT_FILE_OPEN_FAILED ) return 0;
  compat_file_close( fd );
  return 1;
}

/* Return an open handle for the first readable candidate, or NULL. */
static compat_fd
utils_find_auxiliary_file( const char *filename, utils_aux_type type )
{
  char path[ PATH_MAX ];

  if( compat_is_absolute_path( filename ) )
    return compat_file_open( filename, COMPAT_FILE_READ );

  return find_readable_path( filename, path, type );
}


int
utils_find_file_path( const char *filename, char *ret_path,
		      utils_aux_type type )
{
  compat_fd fd;

  /* If given an absolute path, just look there */
  if( compat_is_absolute_path( filename ) ) {
    strncpy( ret_path, filename, PATH_MAX );
    ret_path[ PATH_MAX - 1 ] = '\0';
    return 0;
  }

  fd = find_readable_path( filename, ret_path, type );
  if( fd == COMPAT_FILE_OPEN_FAILED ) return 1;
  compat_file_close( fd );
  return 0;
}

static compat_fd
find_readable_path( const char *filename, char *ret_path, utils_aux_type type )
{
  path_context ctx;

  init_path_context( &ctx, type );

  while( compat_get_next_path( &ctx ) ) {
    int bytes_written;
#ifdef AMIGA
    bytes_written = snprintf( ret_path, PATH_MAX, "%s%s", ctx.path, filename );
#else
    bytes_written = snprintf( ret_path, PATH_MAX, "%s" FUSE_DIR_SEP_STR "%s",
        ctx.path, filename );
#endif
    if( bytes_written >= 0 && bytes_written < PATH_MAX ) {
      compat_fd fd = compat_file_open( ret_path, COMPAT_FILE_READ );
      if( fd != COMPAT_FILE_OPEN_FAILED ) return fd;
    }
  }

  return COMPAT_FILE_OPEN_FAILED;
}

/* Something similar to that described at
   http://www.chiark.greenend.org.uk/~sgtatham/coroutines.html was
   _very_ tempting here...
*/
static void
init_path_context( path_context *ctx, utils_aux_type type )
{
  ctx->state = 0;
  ctx->type = type;
}

void
utils_file_init( utils_file *file, const char *filename )
{
  libspectrum_file_init( file );
  if( filename ) file->filename = utils_safe_strdup( filename );
}

static int
utils_file_resolve( utils_file *file )
{
  libspectrum_file resolved;
  libspectrum_byte *buffer = file->buffer;
  size_t length = file->length;
  libspectrum_error error;

  libspectrum_file_init( &resolved );
  file->buffer = NULL;
  file->length = 0;
  error = libspectrum_file_take_data( &resolved, file->filename, buffer,
                                      length );
  if( error ) {
    utils_file_free( file );
    return error;
  }

  utils_file_free( file );
  *file = resolved;
  return 0;
}

void
utils_file_move( utils_file *destination, utils_file *source )
{
  utils_file_free( destination );
  *destination = *source;
  utils_file_init( source, NULL );
}

int
utils_file_read( utils_file *file )
{
  compat_fd fd;

  if( file->buffer ) return 0;

  fd = compat_file_open( file->filename, 0 );
  if( fd == COMPAT_FILE_OPEN_FAILED ) {
    ui_error( UI_ERROR_ERROR, "couldn't open '%s': %s", file->filename,
              strerror( errno ) );
    return 1;
  }

  return utils_read_fd( fd, file->filename, file );
}

int
utils_file_identify( utils_file *file )
{
  utils_file header, remainder;
  compat_fd fd;
  int64_t length;
  libspectrum_id_t filename_type;
  libspectrum_class_t filename_class;
  int error;

  if( file->type != LIBSPECTRUM_ID_UNKNOWN ||
      file->file_class != LIBSPECTRUM_CLASS_UNKNOWN ) return 0;

  if( file->buffer ) return utils_file_resolve( file );

  fd = compat_file_open( file->filename, 0 );
  if( fd == COMPAT_FILE_OPEN_FAILED ) {
    ui_error( UI_ERROR_ERROR, "couldn't open '%s': %s", file->filename,
              strerror( errno ) );
    return 1;
  }

  length = compat_file_get_length( fd );
  if( length < 0 || (uint64_t)length > SIZE_MAX ) {
    compat_file_close( fd );
    return 1;
  }

  utils_file_init( &header, file->filename );
  header.length = length < UTILS_IDENTIFY_LENGTH ?
                  length : UTILS_IDENTIFY_LENGTH;
  header.buffer = libspectrum_new( unsigned char, header.length );
  if( compat_file_read( fd, header.buffer, header.length ) ) {
    utils_file_free( &header );
    compat_file_close( fd );
    return 1;
  }

  /* Identify HDF images from their signature so they need not be read in
     full. Passing no filename prevents an .hdf extension from masking an
     invalid header. */
  error = libspectrum_identify_file_raw( &file->type, NULL, header.buffer,
                                        header.length );
  if( !error )
    error = libspectrum_identify_class( &file->file_class, file->type );
  if( error || file->file_class == LIBSPECTRUM_CLASS_HARDDISK ) {
    if( file->file_class == LIBSPECTRUM_CLASS_HARDDISK )
      file->storage = LIBSPECTRUM_FILE_STORAGE_PATH;
    utils_file_free( &header );
    if( compat_file_close( fd ) ) return 1;
    return error;
  }

  error = libspectrum_identify_file_raw( &filename_type, file->filename,
                                        NULL, 0 );
  if( !error )
    error = libspectrum_identify_class( &filename_class, filename_type );
  if( error || filename_class == LIBSPECTRUM_CLASS_HARDDISK ) {
    utils_file_free( &header );
    if( compat_file_close( fd ) ) return 1;
    file->type = LIBSPECTRUM_ID_UNKNOWN;
    file->file_class = LIBSPECTRUM_CLASS_UNKNOWN;
    return error;
  }

  file->type = LIBSPECTRUM_ID_UNKNOWN;
  file->file_class = LIBSPECTRUM_CLASS_UNKNOWN;

  file->length = length;
  file->buffer = libspectrum_new( unsigned char, file->length );
  memcpy( file->buffer, header.buffer, header.length );

  utils_file_init( &remainder, NULL );
  remainder.buffer = file->buffer + header.length;
  remainder.length = file->length - header.length;
  utils_file_free( &header );

  if( remainder.length && compat_file_read( fd, remainder.buffer, remainder.length ) ) {
    libspectrum_free( file->buffer );
    file->buffer = NULL;
    file->length = 0;
    compat_file_close( fd );
    return 1;
  }
  if( compat_file_close( fd ) ) {
    libspectrum_free( file->buffer );
    file->buffer = NULL;
    file->length = 0;
    return 1;
  }

  return utils_file_resolve( file );
}

int
utils_read_file( const char *filename, utils_file *file )
{
  utils_file_init( file, filename );
  return utils_file_read( file );
}

int
utils_read_fd( compat_fd fd, const char *filename, utils_file *file )
{
  int64_t length = compat_file_get_length( fd );
  if( length < 0 || (uint64_t)length > SIZE_MAX ) {
    compat_file_close( fd );
    return 1;
  }
  file->length = (size_t)length;

  file->buffer = libspectrum_new( unsigned char, file->length );

  if( compat_file_read( fd, file->buffer, file->length ) ) {
    libspectrum_free( file->buffer );
    file->buffer = NULL;
    compat_file_close( fd );
    return 1;
  }

  if( compat_file_close( fd ) ) {
    ui_error( UI_ERROR_ERROR, "Couldn't close '%s': %s", filename,
	      strerror( errno ) );
    libspectrum_free( file->buffer );
    file->buffer = NULL;
    return 1;
  }

  return utils_file_resolve( file );
}

void
utils_file_free( utils_file *file )
{
  libspectrum_file_clear( file );
}

void
utils_close_file( utils_file *file )
{
  utils_file_free( file );
}

int utils_write_file( const char *filename, const unsigned char *buffer,
		      size_t length )
{
  compat_fd fd;

  fd = compat_file_open( filename, 1 );
  if( fd == COMPAT_FILE_OPEN_FAILED ) {
    ui_error( UI_ERROR_ERROR, "couldn't open `%s' for writing: %s\n",
    	      filename, strerror( errno ) );
    return 1;
  }

  if( compat_file_write( fd, buffer, length ) ) {
    compat_file_close( fd );
    return 1;
  }

  if( compat_file_close( fd ) ) return 1;

  return 0;
}

int
utils_read_auxiliary_file( const char *filename, utils_file *file,
                           utils_aux_type type )
{
  int error;
  compat_fd fd;

  fd = utils_find_auxiliary_file( filename, type );
  if( fd == COMPAT_FILE_OPEN_FAILED ) return -1;

  utils_file_init( file, filename );
  error = utils_read_fd( fd, filename, file );
  if( error ) return error;

  return 0;
}

int
utils_read_screen( const char *filename, utils_file *screen )
{
  int error;

  error = utils_read_auxiliary_file( filename, screen, UTILS_AUXILIARY_LIB );
  if( error == -1 ) {
    ui_error( UI_ERROR_ERROR, "couldn't find screen picture ('%s')",
	      filename );
    return 1;
  }

  if( error ) return error;

  if( screen->length != STANDARD_SCR_SIZE ) {
    utils_close_file( screen );
    ui_error( UI_ERROR_ERROR, "screen picture ('%s') is not %d bytes long",
	      filename, STANDARD_SCR_SIZE );
    return 1;
  }

  return 0;
}

int
utils_save_binary( libspectrum_word start, size_t length,
                   const char *filename )
{
  libspectrum_byte *buffer;
  size_t i;
  int error = 0;

  buffer = libspectrum_new( libspectrum_byte, length );

  for( i = 0; i < length; i++ )
    buffer[ i ] = readbyte_internal( start + i );

  error = utils_write_file( filename, buffer, length );

  libspectrum_free( buffer );

  return error;
}

void
utils_networking_init( void )
{
#ifdef HAVE_SOCKETS

  if( !networking_init_count )
    compat_socket_networking_init();

#endif

  networking_init_count++;
}

void
utils_networking_end( void )
{
  networking_init_count--;

#ifdef HAVE_SOCKETS
  
  if( !networking_init_count )
    compat_socket_networking_end();

#endif
}

