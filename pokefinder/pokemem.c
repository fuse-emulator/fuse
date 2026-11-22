/* pokemem.c: help with handling pokes
   Copyright (c) 2011-2018 Philip Kendall, Sergio Baldoví

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

#include <ctype.h>
#include <string.h>

#include "compat.h"
#include "fuse-engine.h"
#include "machine.h"
#include "memory_pages.h"
#include "pokemem.h"
#include "spectrum.h"
#include "utils.h"

enum {
  POKEFILE_NEXT_TRAINER = 'N',
  POKEFILE_MORE_POKE = 'M',
  POKEFILE_LAST_POKE = 'Z',
  POKEFILE_EOF = 'Y'
};

/* Expected data while parsing a file */
typedef enum do_t {
  DO_TRAINER,
  DO_POKE,
  DO_EOF,
} do_t;

char *pokfile = NULL;              /* Path of a .pok file to load */
GSList *trainer_list = NULL;       /* Trainers loaded from a file */
trainer_t *current_trainer = NULL; /* Last trainer parsed */

static uint64_t trainer_generation;

static void pokemem_read_from_buffer( const libspectrum_byte *buffer,
                                      size_t length );
static void pokemem_read_poke( const libspectrum_byte **ptr,
                               const libspectrum_byte *end );
static void pokemem_read_trainer( const libspectrum_byte **ptr,
                                  const libspectrum_byte *end );
static void pokemem_skip_line( const libspectrum_byte **ptr,
                               const libspectrum_byte *end );
static poke_t *pokemem_poke_add( trainer_t *trainer, int bank, int address,
                                 int value, int restore );

static void pokemem_poke_activate( gpointer data, gpointer user_data );
static void pokemem_poke_deactivate( gpointer data, gpointer user_data );

static libspectrum_byte pokemem_mem_value( libspectrum_word bank,
                                           libspectrum_word address );

static void
pokemem_poke_free( gpointer data, gpointer user_data GCC_UNUSED )
{
  poke_t *poke = data;

  libspectrum_free( poke );
}

static void
pokemem_trainer_free( gpointer data, gpointer user_data GCC_UNUSED )
{
  trainer_t *trainer = data;

  if( !trainer ) return;

  pokemem_trainer_deactivate( trainer );

  if( trainer->poke_list ) {
    g_slist_foreach( trainer->poke_list, pokemem_poke_free, NULL );
    g_slist_free( trainer->poke_list );
  }

  libspectrum_free( trainer->name );
  libspectrum_free( trainer );
}

void
pokemem_clear( void )
{
  trainer_generation++;
  if( trainer_list ) {
    g_slist_foreach( trainer_list, pokemem_trainer_free, NULL );
    g_slist_free( trainer_list );
    trainer_list = NULL;
  }

  libspectrum_free( pokfile );
  pokfile = NULL;
  current_trainer = NULL;
}

void
pokemem_end( void )
{
  pokemem_clear();
}

int
pokemem_read_from_file( const char *filename )
{
  utils_file file;
  int error;

  if( !filename ) return 1;

  pokemem_clear();

  error = utils_read_file( filename, &file );
  if( !error ) {
    pokfile = utils_safe_strdup( filename );
    pokemem_read_from_buffer( file.buffer, file.length );
  }
  utils_close_file( &file );
  return error;
}

void
pokemem_read_loaded( const libspectrum_file *file )
{
  pokemem_clear();
  pokfile = utils_safe_strdup( file->filename );
  pokemem_read_from_buffer( file->buffer, file->length );
}

static void
pokemem_read_from_buffer( const libspectrum_byte *buffer, size_t length )
{
  const libspectrum_byte *ptr, *end;
  libspectrum_byte id;
  int eop;
  do_t do_now;

  trainer_list = NULL;
  current_trainer = NULL;
  ptr = buffer;
  end = buffer + length;
  do_now = DO_TRAINER;
  eop = ( ptr >= end );

  while( !eop ) {

    id = *ptr++; /* First char of a line */

    switch( id ) {

    case POKEFILE_NEXT_TRAINER:
      if( do_now != DO_TRAINER ) {
        /* Unexpected trainer, but parse it */
        if( current_trainer ) current_trainer->disabled = 1;
      }

      pokemem_read_trainer( &ptr, end );
      do_now = DO_POKE;
      break;

    case POKEFILE_MORE_POKE:
      if( do_now != DO_POKE ) {
        /* Skip unexpected poke */
        if( current_trainer ) current_trainer->disabled = 1;
        pokemem_skip_line( &ptr, end );
        do_now = DO_TRAINER;
        break;
      }

      pokemem_read_poke( &ptr, end );
      do_now = DO_POKE;
      break;

    case POKEFILE_LAST_POKE:
      /* Skip unexpected poke */
      if( do_now != DO_POKE ) {
        if( current_trainer ) current_trainer->disabled = 1;
        pokemem_skip_line( &ptr, end );
        do_now = DO_TRAINER;
        break;
      }

      pokemem_read_poke( &ptr, end );
      do_now = DO_TRAINER;
      break;

    case POKEFILE_EOF:
      eop = 1;
      if( do_now == DO_TRAINER ) do_now = DO_EOF;
      break;

    default:
      /* Unknown line */
      if( do_now == DO_POKE ) {
        /* Invalidate current trainer */
        if( current_trainer ) current_trainer->disabled = 1;
        do_now = DO_TRAINER;
      }

      pokemem_skip_line( &ptr, end );
    }

    if( ptr >= end ) eop = 1;
  }

  if( do_now != DO_EOF ) {
    if( current_trainer ) current_trainer->disabled = 1;
  }
}

static void
pokemem_skip_line( const libspectrum_byte **ptr,
                   const libspectrum_byte *end )
{
  const libspectrum_byte *cpos = *ptr;

  /* skip data */
  while( cpos < end && ( *cpos != '\r' && *cpos != '\n' ) ) cpos++;

  /* skip 'new line' like chars */
  while( cpos < end && ( *cpos == '\r' || *cpos == '\n' ) ) cpos++;

  *ptr = cpos;
}

static void
pokemem_read_trainer( const libspectrum_byte **ptr,
                      const libspectrum_byte *end )
{
  const libspectrum_byte *cpos = *ptr;
  const libspectrum_byte *clast;
  char *title;
  size_t length = 0;

  /* get trainer length */
  while( cpos < end && ( *cpos != '\0' && *cpos != '\r' && *cpos != '\n' ) )
    cpos++;

  /* trim trailing spaces */
  clast = cpos;
  while( clast >= *ptr && isspace( *clast ) )
    clast--;

  /* store data */
  length = clast - *ptr + 1;
  if( length > 80 ) length = 80;
  title = libspectrum_new( char, length + 1 );

  memcpy( title, *ptr, length );
  title[ length ] = '\0';

  current_trainer = libspectrum_new0( trainer_t, 1 );
  current_trainer->name = title;
  trainer_list = g_slist_append( trainer_list, current_trainer );

  /* skip 'new line' like chars */
  while( cpos < end && ( *cpos == '\r' || *cpos == '\n' ) ) cpos++;

  *ptr = cpos;
}

static void
pokemem_read_poke( const libspectrum_byte **ptr, const libspectrum_byte *end )
{
  int bank, address, value, restore;
  int items;
  const libspectrum_byte *cpos = *ptr;

  items = sscanf( (const char *)cpos, "%1d %5d %3d %3d",
                  &bank, &address, &value, &restore );

  /* skip data */
  pokemem_skip_line( ptr, end );

  /* validate data */
  if( items < 4 ) {
    current_trainer->disabled = 1;
    return;
  }

  pokemem_poke_add( current_trainer, bank, address, value, restore );
}

static poke_t *
pokemem_poke_add( trainer_t *trainer, int bank, int address, int value,
                  int restore )
{
  int poke_active;
  poke_t *current_poke;

  if( address < 0x0000 || address > 0xffff ) {
    trainer->disabled = 1;
    return NULL;
  }

  /* ROM on normal mode memory configuration */
  if( bank == 8 && address < 0x4000 ) {
    trainer->disabled = 1;
    return NULL;
  }

  if( value < 0 || value > 256 ) {
    trainer->disabled = 1;
    return NULL;
  }

  if( restore < 0 || restore > 255 ) {
    trainer->disabled = 1;
    return NULL;
  }

  /* store data */
  current_poke = libspectrum_new( poke_t, 1 );

  current_poke->bank = bank;
  current_poke->address = address;
  current_poke->value = value;
  current_poke->restore = restore;
  if( value == 256 ) trainer->ask_value = 1;

  /* Check if current poke was already applied */
  if( value <= 255 && pokemem_mem_value( bank, address ) == value ) {
    poke_active = 1;
  } else {
    poke_active = 0;
  }

  /* A trainer is active if all its pokes are applied */
  if( !trainer->poke_list ) {
    trainer->active = poke_active;
  } else {
    trainer->active &= poke_active;
  }

  trainer->poke_list = g_slist_append( trainer->poke_list, current_poke );

  return current_poke;
}

static libspectrum_byte
pokemem_mem_value( libspectrum_word bank, libspectrum_word address )
{
  libspectrum_byte value;

  if( bank == 8 ) {
    value = readbyte_internal( address );
  } else {
    value = RAM[ bank ][ address & 0x3fff ];
  }

  return value;
}

trainer_t *
pokemem_trainer_list_add( libspectrum_byte bank, libspectrum_word address,
                          libspectrum_word value )
{
  char *title;

  title = libspectrum_new( char, 19 );
  snprintf( title, 19, "Custom %u,%u", address, value );

  /* Create trainer */
  current_trainer = libspectrum_new0( trainer_t, 1 );
  current_trainer->name = title;

  trainer_list = g_slist_append( trainer_list, current_trainer );

  /* Create poke */
  pokemem_poke_add( current_trainer, bank, address, value, 0 );

  return current_trainer;
}

int
pokemem_trainer_activate( trainer_t *trainer )
{
  if( !trainer || trainer->disabled || !trainer->poke_list ) return 1;

  if( !trainer->active && trainer->poke_list ) {
    g_slist_foreach( trainer->poke_list, pokemem_poke_activate, trainer );
    trainer->active = 1;
  }

  return 0;
}

static void
pokemem_poke_activate( gpointer data, gpointer user_data )
{
  poke_t *poke = data;
  trainer_t *trainer = user_data;
  libspectrum_word bank = poke->bank;
  libspectrum_word address = poke->address;
  libspectrum_byte value;

  /* User custom value? */
  value = ( poke->value > 255 )? trainer->value : poke->value;

  if( bank == 8 ) {
    poke->restore = readbyte_internal( address );
    writebyte_internal( address, value );
  } else {
    address &= 0x3fff;
    poke->restore = RAM[ bank ][ address ];
    RAM[ bank ][ address ] = value;
  }
}

int
pokemem_trainer_deactivate( trainer_t *trainer )
{
  if( !trainer || trainer->disabled || !trainer->poke_list ) return 1;

  if( trainer->active && trainer->poke_list ) {
    g_slist_foreach( trainer->poke_list, pokemem_poke_deactivate, trainer );
    trainer->active = 0;
  }

  return 0;
}

static void
pokemem_poke_deactivate( gpointer data, gpointer user_data GCC_UNUSED )
{
  poke_t *poke = data;
  libspectrum_word bank = poke->bank;
  libspectrum_word address = poke->address;
  libspectrum_byte value = poke->restore;

  if( bank == 8 ) {
    writebyte_internal( address, value );
  } else {
    RAM[ bank ][ address & 0x3fff ] = value;
  }

}

void
fuse_pok_trainers_free( fuse_pok_trainers *info )
{
  size_t i;
  if( !info ) return;
  for( i = 0; i < info->count; i++ ) libspectrum_free( info->trainers[i].name );
  libspectrum_free( info->trainers );
  libspectrum_free( info );
}

fuse_host_operation_result
fuse_host_copy_pok_trainers( fuse_pok_trainers **info )
{
  fuse_pok_trainers *copy;
  GSList *item;
  size_t i = 0;
  fuse_host_operation_result result;
  if( !info ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *info = NULL;
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  copy = libspectrum_new0( fuse_pok_trainers, 1 );
  copy->generation = trainer_generation;
  copy->count = g_slist_length( trainer_list );
  if( copy->count ) copy->trainers = libspectrum_new( fuse_pok_trainer,
                                                      copy->count );
  for( item = trainer_list; item; item = item->next ) {
    const trainer_t *trainer = item->data;
    fuse_pok_trainer *entry = &copy->trainers[i++];
    entry->name = utils_safe_strdup( trainer->name );
    entry->active = trainer->active;
    entry->disabled = trainer->disabled || !trainer->poke_list;
    entry->asks_value = trainer->ask_value;
  }
  *info = copy;
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_set_pok_trainer( uint64_t generation, size_t ordinal,
                           int enabled, int value )
{
  GSList *item;
  trainer_t *trainer;
  int error, needs_value;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  if( generation != trainer_generation ) return FUSE_HOST_OPERATION_STALE;
  if( enabled != 0 &&
      enabled != 1 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  for( item = trainer_list; item && ordinal; item = item->next ) ordinal--;
  if( !item ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  trainer = item->data;
  if( trainer->disabled ||
      !trainer->poke_list ) return FUSE_HOST_OPERATION_MEDIA_ERROR;
  needs_value = enabled && trainer->ask_value;
  if( needs_value ? value < 0 || value > 255 : value != -1 )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  if( needs_value && !trainer->active ) trainer->value = value;
  error = enabled ? pokemem_trainer_activate( trainer ) :
          pokemem_trainer_deactivate( trainer );
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

/* Open or Drag'n'Drop a .pok file */
int
pokemem_set_pokfile( const char *filename )
{
  pokemem_clear();

  if( !utils_file_is_readable( filename ) )
    return 1;

  pokfile = utils_safe_strdup( filename );

  return 0;
}

/* Automatic search after the load of snapshots or tapes */
int
pokemem_find_pokfile( const char *path )
{
  int n, has_extension, last_dot, last_slash;
  size_t length, filename_size;
  char *test_file, *c;

  if( pokfile ) return 1; /* Previous .pok file already found */

  length = strlen( path );
  if( !length ) return 1; /* Nothing to search */

  test_file = libspectrum_new( char, length + 11 );

  memcpy( test_file, path, length + 1 );

  c = strrchr( test_file, FUSE_DIR_SEP_CHR );
  last_slash = ( c )? c - test_file : -1;

  c = strrchr( test_file, '.' );
  last_dot = ( c )? c - test_file : -1;

  has_extension = ( last_dot > last_slash + 1 );

  /* Try .pok extension */
  if( has_extension ) {
    n = last_dot; /* Replace file extension */
    test_file[n] = '\0';
  } else {
    n = length; /* Append file extension */
  }

  strcat( test_file, ".pok" );
  if( utils_file_is_readable( test_file ) ) {
    pokfile = test_file;
    return 0;
  }

  /* Try .POK extension */
  /* FIXME: Is filesystem case sensitive? */
  memcpy( &(test_file[n]), ".POK", 4 );
  if( utils_file_is_readable( test_file ) ) {
    pokfile = test_file;
    return 0;
  }

  /* Browse POKES/ directory */
  if( last_slash >= 0 ) {
    n = last_slash + 1; /* insert directory */
    filename_size =
      ( has_extension )? (unsigned int) ( last_dot - last_slash - 1 ) :
                         strlen( &path[n] );
    test_file[ n ] = '\0';
    strcat( test_file, "POKES" );
  } else {
    n = 0; /* prepend directory */
    filename_size = ( has_extension )? (unsigned int) last_dot : length;
    strcpy( test_file, "POKES" );
    test_file[ 5 ] = '\0';
  }

  /* Try .pok extension */
  strcat( test_file, FUSE_DIR_SEP_STR );
  strncat( test_file, &path[ n ], filename_size );
  strcat( test_file, ".pok" );

  if( utils_file_is_readable( test_file ) ) {
    pokfile = test_file;
    return 0;
  }

  /* Try .POK extension */
  /* FIXME: Is filesystem case sensitive? */
  n = n + 6 + filename_size;
  memcpy( &test_file[ n ], ".POK", 4 );

  if( utils_file_is_readable( test_file ) ) {
    pokfile = test_file;
    return 0;
  }

  libspectrum_free( test_file );

  return 1;
}

/* Finally load requested from UI */
int
pokemem_autoload_pokfile( void )
{
  utils_file file;
  int error;

  if( !pokfile || trainer_list ) return 1;

  error = utils_read_file( pokfile, &file );
  if( error ) return error;

  pokemem_read_from_buffer( file.buffer, file.length );
  utils_close_file( &file );

  return 0;
}
