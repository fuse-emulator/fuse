/* specplus3.c: Spectrum +2A/+3 specific routines
   Copyright (c) 1999-2017 Philip Kendall, Darren Salt, Gergely Szasz
   Copyright (c) 2016 Gergely Szasz

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

#include <stdio.h>
#include <assert.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "libspectrum.h"

#include "compat.h"
#include "display.h"
#include "rzx.h"
#include "fuse.h"
#include "machine.h"
#include "machines_periph.h"
#include "memory_pages.h"
#include "periph.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/upd_fdc.h"
#include "peripherals/printer.h"
#include "phantom_typist.h"
#include "settings.h"
#include "snapshot.h"
#include "spec128.h"
#include "spec48.h"
#include "specplus3.h"
#include "spectrum.h"
#include "ui/ui.h"
#include "ui/uimedia.h"
#include "utils.h"
#include "options.h"	/* needed for get combo options */

/* Retained ASIC transfer, not the conventional 128K floating bus.
   Fetch phase and same-slot page switching are provisional.
   No per-cycle events are needed. Host RAM edits are not observed. */
int specplus3_bus_active;
static libspectrum_byte bus_value;
static long bus_synced_through;
/* Lower bound on the next unconsumed fetch: exact after calculation/rebase;
   conservative after a CPU transfer supersedes intervening video bytes. */
static long bus_next_fetch;
static long bus_first_fetch, bus_frame_length, bus_line_length;
static int bus_screen_page;

static void
specplus3_bus_sync( long time )
{
  long local, line, slot, fetch;
  int column, offset;

  if( !specplus3_bus_active ) return;
  assert( time >= bus_synced_through );
  if( time < bus_next_fetch ) {
    bus_synced_through = time;
    return;
  }
  local = time % bus_frame_length;
  fetch = time - local;
  if( local < bus_first_fetch ) {
    /* Before this frame's first fetch, the last fetch is in the last
       displayed line of the previous frame. */
    fetch -= bus_frame_length;
    line = DISPLAY_HEIGHT - 1;
    slot = 123;
  } else {
    line = ( local - bus_first_fetch ) / bus_line_length;
    slot = ( local - bus_first_fetch ) % bus_line_length;
    if( line >= DISPLAY_HEIGHT ) {
      line = DISPLAY_HEIGHT - 1;
      slot = 123;
    } else if( slot > 123 ) {
      slot = 123;
    } else if( ( slot & 7 ) > 3 ) {
      slot = ( slot & ~7 ) + 3;
    }
  }
  fetch += bus_first_fetch + line * bus_line_length + slot;
  if( fetch > bus_synced_through ) {
    column = 2 * ( slot / 8 ) + ( slot & 3 ) / 2;
    offset = ( slot & 1 ) ? display_attr_start[ line ] :
                           display_line_start[ line ];
    bus_value = RAM[ bus_screen_page ][ offset + column ];
  }
  /* First legal slot strictly after time, from the latest legal slot.
     Idle group slots, horizontal border and vertical blank remain distinct. */
  if( ( slot & 7 ) < 3 )
    bus_next_fetch = fetch + 1;
  else if( slot < 123 )
    bus_next_fetch = fetch + 5;
  else if( line < DISPLAY_HEIGHT - 1 )
    bus_next_fetch = fetch + bus_line_length - 123;
  else
    bus_next_fetch = fetch + bus_frame_length -
                     ( DISPLAY_HEIGHT - 1 ) * bus_line_length - 123;
  assert( bus_next_fetch > time );
  bus_synced_through = time;
}

void
specplus3_bus_invalidate( void )
{
  specplus3_bus_active = 0;
}

void
specplus3_bus_rebase( void )
{
  specplus3_bus_active = machine_current && !rzx_playback &&
    machine_current->unattached_port_for_port == specplus3_unattached_port;
  bus_value = 0xff; /* Emulator reset/restore policy, not a power-on claim. */
  bus_synced_through = tstates;
  bus_screen_page = memory_current_screen;
  if( specplus3_bus_active ) {
    bus_first_fetch = machine_current->line_times[ DISPLAY_BORDER_HEIGHT ] +
                      DISPLAY_BORDER_WIDTH_COLS * 4 + 2;
    bus_frame_length = machine_current->timings.tstates_per_frame;
    bus_line_length = machine_current->timings.tstates_per_line;
    /* Initialize the next slot without replaying RAM at/before this clock:
       the cursor already equals time, so sync cannot latch an old fetch. */
    bus_next_fetch = tstates;
    specplus3_bus_sync( tstates );
  }
}

void
specplus3_bus_frame( libspectrum_dword length, libspectrum_dword now )
{
  if( !specplus3_bus_active ) return;
  specplus3_bus_sync( now );
  bus_synced_through -= length;
  bus_next_fetch -= length;
}

static void
specplus3_bus_screen_change( int page, libspectrum_dword time )
{
  if( !specplus3_bus_active ) return;
  /* Old source strictly before S; the new source owns an unconsumed S. */
  if( (long)time > bus_synced_through ) specplus3_bus_sync( (long)time - 1 );
  bus_screen_page = page;
}

void
specplus3_bus_transfer( libspectrum_dword time, libspectrum_byte value )
{
  /* Every video transfer through this sample is superseded by the CPU
     byte, so consuming that history needs no RAM peek or fetch arithmetic.
     If the exact next slot is no longer known, time + 1 is a conservative
     bound: a later sync recomputes rather than skipping a possible fetch.
     In particular, M1 refresh time is NOT consumed by this notification. */
  assert( (long)time >= bus_synced_through );
  bus_synced_through = time;
  if( (long)time >= bus_next_fetch ) bus_next_fetch = (long)time + 1;
  bus_value = value;
}

libspectrum_byte
specplus3_unattached_port( libspectrum_word port )
{
  if( !specplus3_bus_active || ( port & 0xf003 ) != 1 ||
      machine_current->ram.locked ) return 0xff;
  specplus3_bus_sync( tstates );
  return bus_value | 1;
}

static int normal_memory_map( int rom, int page );
static void special_memory_map( int which );
static void select_special_map( int page1, int page2, int page3, int page4 );

static int specplus3_reset( int hard_reset, libspectrum_snap *snap );

upd_fdc *specplus3_fdc;
static fdd_t specplus3_drives[ SPECPLUS3_NUM_DRIVES ];

static int ui_drive_is_available( void );
static const fdd_params_t *ui_drive_get_params_a( void );
static const fdd_params_t *ui_drive_get_params_b( void );
static int ui_drive_inserted( const ui_media_drive_info_t *drive, int new );
static int ui_drive_autoload( void );

static ui_media_drive_info_t ui_drives[ SPECPLUS3_NUM_DRIVES ] = {
  {
    /* .name = */ "+3 Disk A:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_PLUS3,
    /* .drive_index = */ SPECPLUS3_DRIVE_A,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3,
    /* .menu_item_top = */ UI_MENU_ITEM_INVALID,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_A_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_A_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_A_WP_SET,
    /* .is_available = */ &ui_drive_is_available,
    /* .get_params = */ &ui_drive_get_params_a,
    /* .insert_hook = */ &ui_drive_inserted,
    /* .autoload_hook = */ &ui_drive_autoload,
  },
  {
    /* .name = */ "+3 Disk B:",
    /* .controller_index = */ UI_MEDIA_CONTROLLER_PLUS3,
    /* .drive_index = */ SPECPLUS3_DRIVE_B,
    /* .menu_item_parent = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3,
    /* .menu_item_top = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_B,
    /* .menu_item_eject = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_B_EJECT,
    /* .menu_item_flip = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_B_FLIP_SET,
    /* .menu_item_wp = */ UI_MENU_ITEM_MEDIA_DISK_PLUS3_B_WP_SET,
    /* .is_available = */ &ui_drive_is_available,
    /* .get_params = */ &ui_drive_get_params_b,
    /* .insert_hook = */ &ui_drive_inserted,
    /* .autoload_hook = */ &ui_drive_autoload,
  },
};

int
specplus3_port_from_ula( libspectrum_word port GCC_UNUSED )
{
  /* No contended ports */
  return 0;
}

int specplus3_init( fuse_machine_info *machine )
{
  machine->machine = LIBSPECTRUM_MACHINE_PLUS3;
  machine->id = "plus3";

  machine->reset = specplus3_reset;

  machine->timex = 0;
  machine->ram.port_from_ula	     = specplus3_port_from_ula;
  machine->ram.contend_delay	     = spectrum_contend_delay_76543210;
  machine->ram.contend_delay_no_mreq = spectrum_contend_delay_none;
  machine->ram.valid_pages	     = 8;

  machine->unattached_port = spectrum_unattached_port_none;
  machine->unattached_port_for_port = specplus3_unattached_port;

  specplus3_765_init();
  specplus3_menu_items();

  machine->shutdown = specplus3_shutdown;

  machine->memory_map = specplus3_memory_map;

  return 0;
}

void
specplus3_765_update_fdd( void )
{
  specplus3_fdc->speedlock = settings_current.plus3_detect_speedlock ? 0 : -1;
}

void
specplus3_765_init( void )
{
  int i;

  specplus3_fdc = upd_fdc_alloc_fdc( UPD765A, UPD_CLOCK_4MHZ );
  /*!!!! the plus3 only use the US0 pin to select drives,
   so drive 2 := drive 0 and drive 3 := drive 1 !!!!*/
  specplus3_fdc->drive[0] = &specplus3_drives[ 0 ];
  specplus3_fdc->drive[1] = &specplus3_drives[ 1 ];
  specplus3_fdc->drive[2] = &specplus3_drives[ 0 ];
  specplus3_fdc->drive[3] = &specplus3_drives[ 1 ];

  for( i = 0; i < SPECPLUS3_NUM_DRIVES; i++ ) {
    specplus3_drives[ i ].disk.flag = DISK_FLAG_PLUS3_CPC;
  }
					/* builtin drive 1 head 42 track */
  fdd_init( &specplus3_drives[ 0 ], FDD_SHUGART, &fdd_params[ 1 ], 0 );
  fdd_init( &specplus3_drives[ 1 ], FDD_SHUGART, NULL, 0 );	/* drive geometry 'autodetect' */
  specplus3_fdc->set_intrq = NULL;
  specplus3_fdc->reset_intrq = NULL;
  specplus3_fdc->set_datarq = NULL;
  specplus3_fdc->reset_datarq = NULL;

  specplus3_765_update_fdd();

  for( i = 0; i < SPECPLUS3_NUM_DRIVES; i++ ) {
    ui_drives[ i ].fdd = &specplus3_drives[ i ];
    ui_media_drive_register( &ui_drives[ i ] );
  }
}

void
specplus3_765_reset( int hard_reset )
{
  const fdd_params_t *dt;

  upd_fdc_master_reset( specplus3_fdc );
  dt = &fdd_params[ option_enumerate_diskoptions_drive_plus3a_type() + 1 ];	/* +1 => there is no `Disabled' */
  fdd_init( &specplus3_drives[ 0 ], FDD_SHUGART, dt, 1 );

  dt = &fdd_params[ option_enumerate_diskoptions_drive_plus3b_type() ];
  fdd_init( &specplus3_drives[ 1 ], dt->enabled ? FDD_SHUGART : FDD_TYPE_NONE,
            dt, 1 );
}

static int
specplus3_reset( int hard_reset, libspectrum_snap *snap )
{
  int error;

  error = machine_load_rom( 0, settings_current.rom_plus3_0,
                            settings_default.rom_plus3_0, 0x4000, snap );
  if( error ) return error;
  error = machine_load_rom( 1, settings_current.rom_plus3_1,
                            settings_default.rom_plus3_1, 0x4000, snap );
  if( error ) return error;
  error = machine_load_rom( 2, settings_current.rom_plus3_2,
                            settings_default.rom_plus3_2, 0x4000, snap );
  if( error ) return error;
  error = machine_load_rom( 3, settings_current.rom_plus3_3,
                            settings_default.rom_plus3_3, 0x4000, snap );
  if( error ) return error;

  error = specplus3_plus2a_common_reset( hard_reset );
  if( error ) return error;

  periph_clear();
  machines_periph_plus3();

  periph_set_present( PERIPH_TYPE_UPD765, PERIPH_PRESENT_ALWAYS );

  periph_update();

  specplus3_765_reset( hard_reset );
  specplus3_menu_items();

  spec48_common_display_setup();

  return 0;
}

int
specplus3_plus2a_common_reset( int hard_reset )
{
  int error;
  size_t i;

  machine_current->ram.current_page=0; machine_current->ram.current_rom=0;
  machine_current->ram.locked=0;
  machine_current->ram.special=0;
  machine_current->ram.last_byte=0;
  machine_current->ram.last_byte2=0;

  memory_current_screen = 5;
  memory_screen_mask = 0xffff;

  /* All memory comes from the home bank */
  for( i = 0; i < MEMORY_PAGES_IN_64K; i++ )
    memory_map_read[i].source = memory_map_write[i].source = memory_source_ram;

  /* RAM pages 4, 5, 6 and 7 contended */
  for( i = 0; i < 8; i++ )
    memory_ram_set_16k_contention( i, i >= 4 );

  error = normal_memory_map( 0, 0 ); if( error ) return error;

  return 0;
}

static int
normal_memory_map( int rom, int page )
{
  /* ROM as specified */
  memory_map_16k( 0x0000, memory_map_rom, rom );
  /* RAM 5 */
  memory_map_16k( 0x4000, memory_map_ram, 5 );
  /* RAM 2 */
  memory_map_16k( 0x8000, memory_map_ram, 2 );
  /* RAM 0 */
  memory_map_16k( 0xc000, memory_map_ram, page );

  return 0;
}

static void
special_memory_map( int which )
{
  switch( which ) {
  case 0: select_special_map( 0, 1, 2, 3 ); break;
  case 1: select_special_map( 4, 5, 6, 7 ); break;
  case 2: select_special_map( 4, 5, 6, 3 ); break;
  case 3: select_special_map( 4, 7, 6, 3 ); break;

  default:
    ui_error( UI_ERROR_ERROR, "unknown +3 special configuration %d", which );
    fuse_abort();
  }
}

static void
select_special_map( int page1, int page2, int page3, int page4 )
{
  memory_map_16k( 0x0000, memory_map_ram, page1 );
  memory_map_16k( 0x4000, memory_map_ram, page2 );
  memory_map_16k( 0x8000, memory_map_ram, page3 );
  memory_map_16k( 0xc000, memory_map_ram, page4 );
}

void
specplus3_memoryport2_write_internal( libspectrum_word port GCC_UNUSED,
                                      libspectrum_byte b )
{
  /* Let the parallel printer code know about the strobe bit */
  printer_parallel_strobe_write( b & 0x10 );

  /* If this was called by a machine which has a +3-style disk, set
     the state of both floppy drive motors */
  if( machine_current->capabilities &&
      LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_DISK ) {

    fdd_motoron( &specplus3_drives[0], b & 0x08 );
    fdd_motoron( &specplus3_drives[1], b & 0x08 );
  }

  /* Store the last byte written in case we need it */
  machine_current->ram.last_byte2 = b;

  machine_current->memory_map();
}

void
specplus3_memoryport2_write( libspectrum_word port, libspectrum_byte b )
{
  /* Do nothing else if we've locked the RAM configuration */
  if( machine_current->ram.locked ) return;

  specplus3_memoryport2_write_internal( port, b );
}

int
specplus3_memory_map( void )
{
  int page, rom, screen;

  page = machine_current->ram.last_byte & 0x07;
  screen = ( machine_current->ram.last_byte & 0x08 ) ? 7 : 5;
  rom =
    ( ( machine_current->ram.last_byte  & 0x10 ) >> 4 ) |
    ( ( machine_current->ram.last_byte2 & 0x04 ) >> 1 );

  /* If we changed the active screen, mark the entire display file as
     dirty so we redraw it on the next pass */
  if( memory_current_screen != screen ) {
    display_update_critical( 0, 0 );
    display_refresh_main_screen();
    specplus3_bus_screen_change( screen, tstates );
    memory_current_screen = screen;
  }

  /* Check whether we want a special RAM configuration */
  if( machine_current->ram.last_byte2 & 0x01 ) {

    /* If so, select it */
    machine_current->ram.special = 1;
    special_memory_map( ( machine_current->ram.last_byte2 & 0x06 ) >> 1 );

  } else {

    /* If not, we're selecting the high bit of the current ROM */
    machine_current->ram.special = 0;
    normal_memory_map( rom, page );

  }

  machine_current->ram.current_page = page;
  machine_current->ram.current_rom  = rom;

  memory_romcs_map();

  return 0;
}

void
specplus3_menu_items( void )
{
  ui_media_drive_update_menus( &ui_drives[ SPECPLUS3_DRIVE_A ],
			       UI_MEDIA_DRIVE_UPDATE_ALL );
  ui_media_drive_update_menus( &ui_drives[ SPECPLUS3_DRIVE_B ],
			       UI_MEDIA_DRIVE_UPDATE_ALL );
}

libspectrum_byte
specplus3_fdc_status( libspectrum_word port GCC_UNUSED,
                      libspectrum_byte *attached )
{
  *attached = 0xff; /* TODO: check this */
  return upd_fdc_read_status( specplus3_fdc );
}

libspectrum_byte
specplus3_fdc_read( libspectrum_word port GCC_UNUSED,
                    libspectrum_byte *attached )
{
  *attached = 0xff; /* TODO: check this */
  return upd_fdc_read_data( specplus3_fdc );
}

void
specplus3_fdc_write( libspectrum_word port GCC_UNUSED, libspectrum_byte data )
{
  upd_fdc_write_data( specplus3_fdc, data );
}

/* FDC UI related functions */

int
specplus3_disk_insert( specplus3_drive_number which, const char *filename,
		   int autoload )
{
  if( which >= SPECPLUS3_NUM_DRIVES ) {
    ui_error( UI_ERROR_ERROR, "specplus3_disk_insert: unknown drive %d",
	      which );
    fuse_abort();
  }

  return ui_media_drive_insert( &ui_drives[ which ], filename, autoload );
}

fdd_t *
specplus3_get_fdd( specplus3_drive_number which )
{
  return &( specplus3_drives[ which ] );
}

static int
ui_drive_is_available( void )
{
  return machine_current->capabilities &
          LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_DISK;
}

static const fdd_params_t *
ui_drive_get_params_a( void )
{
  /* +1 => there is no `Disabled' */
  return &fdd_params[ option_enumerate_diskoptions_drive_plus3a_type() + 1 ];
}

static const fdd_params_t *
ui_drive_get_params_b( void )
{
  return &fdd_params[ option_enumerate_diskoptions_drive_plus3b_type() ];
}

static int
ui_drive_inserted( const ui_media_drive_info_t *drive, int new )
{
  /* Did real +3 hardware also have problems formatting disks unformatted
     disks? */
  if( new )
    disk_preformat( &drive->fdd->disk );	/* pre-format disk for +3 ??? */
  return 0;
}

int
specplus3_shutdown( void )
{
  /* +3 and +3e share this controller and shutdown callback. */
  libspectrum_free( specplus3_fdc );
  specplus3_fdc = NULL;
  return 0;
}

static int
ui_drive_autoload( void )
{
  machine_reset( 0 );
  phantom_typist_activate_disk();
  return 0;
}
