/* pokefinder.c: help with finding pokes
   Copyright (c) 2003-2012 Philip Kendall

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

#include <string.h>

#include "libspectrum.h"

#include "fuse-engine.h"
#include "fuse-host-memory.h"
#include "machine.h"
#include "memory_pages.h"
#include "pokefinder.h"
#include "spectrum.h"

#define POKEFINDER_PAGE_COUNT \
  ( MEMORY_PAGES_IN_16K * SPECTRUM_RAM_PAGES )
#define POKEFINDER_POSSIBLE_SIZE \
  ( POKEFINDER_PAGE_COUNT * MEMORY_PAGE_SIZE )
#define POKEFINDER_IMPOSSIBLE_SIZE \
  ( POKEFINDER_PAGE_COUNT * MEMORY_PAGE_SIZE / 8 )

libspectrum_byte (*pokefinder_possible)[ MEMORY_PAGE_SIZE ];
libspectrum_byte (*pokefinder_impossible)[ MEMORY_PAGE_SIZE / 8 ];
size_t pokefinder_count;

static int pokefinder_allocated;
static int host_search_valid;

void
pokefinder_invalidate_host_search( void )
{
  host_search_valid = 0;
}

void
pokefinder_end( void )
{
  host_search_valid = pokefinder_allocated = 0;
  pokefinder_count = 0;
  pokefinder_possible = NULL;
  pokefinder_impossible = NULL;
}

static void
pokefinder_allocate( void )
{
  if( pokefinder_allocated ) return;

  pokefinder_possible = (libspectrum_byte (*)[ MEMORY_PAGE_SIZE ])
    memory_pool_allocate_persistent( POKEFINDER_POSSIBLE_SIZE, 1 );
  pokefinder_impossible = (libspectrum_byte (*)[ MEMORY_PAGE_SIZE / 8 ])
    memory_pool_allocate_persistent( POKEFINDER_IMPOSSIBLE_SIZE, 1 );
  pokefinder_allocated = 1;
}

int
pokefinder_is_allocated( void )
{
  return pokefinder_allocated;
}

void
pokefinder_clear( void )
{
  size_t page, max_page;

  pokefinder_allocate();

  max_page = MEMORY_PAGES_IN_16K * machine_current->ram.valid_pages;
  pokefinder_count = 0;
  for( page = 0; page < POKEFINDER_PAGE_COUNT; ++page )
    if( page < max_page && memory_map_ram[page].writable ) {
      pokefinder_count += MEMORY_PAGE_SIZE;
      memcpy( pokefinder_possible[page], memory_map_ram[page].page, MEMORY_PAGE_SIZE );
      memset( pokefinder_impossible[page], 0, MEMORY_PAGE_SIZE / 8 );
    } else
      memset( pokefinder_impossible[page], 255, MEMORY_PAGE_SIZE / 8 );
}

int
pokefinder_search( libspectrum_byte value )
{
  size_t page, offset;

  if( !pokefinder_allocated ) return 0;

  for( page = 0; page < POKEFINDER_PAGE_COUNT; page++ ) {
    memory_page *mapping = &memory_map_ram[ page ];

    for( offset = 0; offset < MEMORY_PAGE_SIZE; offset++ ) {
      if( pokefinder_impossible[page][offset/8] & 1 << (offset & 7) ) continue;

      if( mapping->page[offset] != value ) {
	pokefinder_impossible[page][offset/8] |= 1 << (offset & 7);
	pokefinder_count--;
      }
    }
  }

  return 0;
}

int
pokefinder_incremented( void )
{
  size_t page, offset;

  if( !pokefinder_allocated ) return 0;

  for( page = 0; page < POKEFINDER_PAGE_COUNT; page++ ) {
    memory_page *mapping = &memory_map_ram[ page ];

    for( offset = 0; offset < MEMORY_PAGE_SIZE; offset++ ) {
      if( pokefinder_impossible[page][offset/8] & 1 << (offset & 7) ) continue;

      if( mapping->page[offset] > pokefinder_possible[page][offset] ) {
        pokefinder_possible[page][offset] = mapping->page[offset];
      } else {
	pokefinder_impossible[page][offset/8] |= 1 << (offset & 7);
	pokefinder_count--;
      }

    }
  }

  return 0;
}

int
pokefinder_decremented( void )
{
  size_t page, offset;

  if( !pokefinder_allocated ) return 0;

  for( page = 0; page < POKEFINDER_PAGE_COUNT; page++ ) {
    memory_page *mapping = &memory_map_ram[ page ];

    for( offset = 0; offset < MEMORY_PAGE_SIZE; offset++ ) {
      if( pokefinder_impossible[page][offset/8] & 1 << (offset & 7) ) continue;

      if( mapping->page[offset] < pokefinder_possible[page][offset] ) {
        pokefinder_possible[page][offset] = mapping->page[offset];
      } else {
	pokefinder_impossible[page][offset/8] |= 1 << (offset & 7);
	pokefinder_count--;
      }

    }
  }

  return 0;
}

static fuse_host_operation_result
check_host_search( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result == FUSE_HOST_OPERATION_OK &&
      fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  return result;
}

fuse_host_operation_result
fuse_host_reset_poke_finder( void )
{
  fuse_host_operation_result result = check_host_search();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  pokefinder_clear();
  host_search_valid = 1;
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_filter_poke_finder( fuse_poke_filter filter, libspectrum_byte value )
{
  fuse_host_operation_result result = check_host_search();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( filter < FUSE_POKE_EXACT || filter > FUSE_POKE_DECREMENTED )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( !host_search_valid ) return FUSE_HOST_OPERATION_STALE;
  fuse_engine_begin_operation();
  switch( filter ) {
  case FUSE_POKE_EXACT: pokefinder_search( value ); break;
  case FUSE_POKE_INCREMENTED: pokefinder_incremented(); break;
  case FUSE_POKE_DECREMENTED: pokefinder_decremented(); break;
  }
  fuse_engine_end_operation();
  return result;
}

fuse_host_operation_result
fuse_host_copy_poke_matches( fuse_poke_match **matches, size_t *count )
{
  size_t page, offset, i = 0;
  fuse_host_operation_result result;
  if( !matches || !count ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *matches = NULL;
  *count = 0;
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !host_search_valid ) return FUSE_HOST_OPERATION_STALE;
  if( !pokefinder_count ) return FUSE_HOST_OPERATION_OK;
  *matches = libspectrum_new( fuse_poke_match, pokefinder_count );
  for( page = 0; page < POKEFINDER_PAGE_COUNT; page++ ) {
    const memory_page *mapping = &memory_map_ram[page];
    for( offset = 0; offset < MEMORY_PAGE_SIZE; offset++ ) {
      if( pokefinder_impossible[page][offset / 8] & 1 << ( offset & 7 ) )continue;
      (*matches )[i].bank = mapping->page_num;
      (*matches )[i++].offset = mapping->offset + offset;
    }
  }
  *count = i;
  return FUSE_HOST_OPERATION_OK;
}

int
pokefinder_unittest( void )
{
  size_t page, max_page;
  libspectrum_byte original;
  int r = 0;

  max_page = MEMORY_PAGES_IN_16K * machine_current->ram.valid_pages;
  for( page = 0; page < max_page; page++ )
    if( memory_map_ram[ page ].writable ) break;

  if( page == max_page ) return 0;

  original = memory_map_ram[ page ].page[ 0 ];

  /* --- search: a mismatched byte must be flagged impossible --- */
  memory_map_ram[ page ].page[ 0 ] = original;
  pokefinder_clear();

  if( !pokefinder_is_allocated() ) {
    printf( "pokefinder: expected allocated after pokefinder_clear()\n" );
    r++;
  }

  memory_map_ram[ page ].page[ 0 ] = original ^ 0xff;
  pokefinder_search( original );

  if( !( pokefinder_impossible[ page ][ 0 ] & 1 ) ) {
    printf( "pokefinder: expected impossible bit after search mismatch\n" );
    r++;
  }

  /* --- incremented: increasing a value keeps it possible;
     not increasing marks it impossible --- */
  memory_map_ram[ page ].page[ 0 ] = 0x40;
  pokefinder_clear();
  memory_map_ram[ page ].page[ 0 ] = 0x41;
  pokefinder_incremented();

  if( pokefinder_impossible[ page ][ 0 ] & 1 ) {
    printf( "pokefinder: incremented: byte should still be possible after increase\n" );
    r++;
  }

  pokefinder_incremented(); /* value unchanged — no longer increasing */
  if( !( pokefinder_impossible[ page ][ 0 ] & 1 ) ) {
    printf( "pokefinder: incremented: byte should be impossible when not further increased\n" );
    r++;
  }

  /* --- decremented: decreasing a value keeps it possible;
     not decreasing marks it impossible --- */
  memory_map_ram[ page ].page[ 0 ] = 0x40;
  pokefinder_clear();
  memory_map_ram[ page ].page[ 0 ] = 0x3f;
  pokefinder_decremented();

  if( pokefinder_impossible[ page ][ 0 ] & 1 ) {
    printf( "pokefinder: decremented: byte should still be possible after decrease\n" );
    r++;
  }

  pokefinder_decremented(); /* value unchanged — no longer decreasing */
  if( !( pokefinder_impossible[ page ][ 0 ] & 1 ) ) {
    printf( "pokefinder: decremented: byte should be impossible when not further decreased\n" );
    r++;
  }

  memory_map_ram[ page ].page[ 0 ] = original;
  pokefinder_clear();

  return r;
}
