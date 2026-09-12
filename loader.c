/* loader.c: loader detection
   Copyright (c) 2006-2018 Philip Kendall

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

#include "event.h"
#include "loader.h"
#include "memory_pages.h"
#include "rzx.h"
#include "settings.h"
#include "spectrum.h"
#include "tape.h"
#include "z80/z80.h"

static int successive_reads = 0;
static libspectrum_signed_dword last_tstates_read = -100000;
static libspectrum_byte last_b_read = 0x00;
static int length_known1 = 0, length_known2 = 0;
static int length_long1 = 0, length_long2 = 0;

typedef enum acceleration_mode_t {
  ACCELERATION_MODE_NONE = 0,
  ACCELERATION_MODE_INCREASING,
  ACCELERATION_MODE_DECREASING,
  ACCELERATION_MODE_SOFTWARE_PROJECTS,
} acceleration_mode_t;

static acceleration_mode_t acceleration_mode;
static size_t acceleration_pc;

#define SOFTWARE_PROJECTS_SHORT_PULSE_ITERATIONS 10
#define SOFTWARE_PROJECTS_LONG_PULSE_ITERATIONS 20

void
loader_frame( libspectrum_dword frame_length )
{
  if( last_tstates_read > -100000 ) {
    last_tstates_read -= frame_length;
  }
}

void
loader_tape_play( void )
{
  successive_reads = 0;
  acceleration_mode = ACCELERATION_MODE_NONE;
}

void
loader_tape_stop( void )
{
  successive_reads = 0;
  acceleration_mode = ACCELERATION_MODE_NONE;
}

static void
software_projects_accelerate( int long_pulse )
{
  int iterations = long_pulse ? SOFTWARE_PROJECTS_LONG_PULSE_ITERATIONS :
                                SOFTWARE_PROJECTS_SHORT_PULSE_ITERATIONS;

  z80.bc.b.h = z80.af_.b.h - iterations;

  /* Continue with the loader's edge-found path. */
  z80.pc.w = acceleration_pc + 9;
}

static void
do_acceleration( void )
{
  if( length_known1 ) {
    if( acceleration_mode == ACCELERATION_MODE_SOFTWARE_PROJECTS ) {
      /* The loader converts the number of loop iterations to a pulse length
         by subtracting B from A' and multiplying the result by four. */
      software_projects_accelerate( length_long1 );
    } else {
      /* B is used to indicate the length of the pulses */
      int set_b_high = length_long1;
      set_b_high ^= ( acceleration_mode == ACCELERATION_MODE_DECREASING );
      if( set_b_high ) {
        z80.bc.b.h = 0xfe;
      } else {
        z80.bc.b.h = 0x00;
      }

      /* Bit 5 of C is used to indicate the current microphone level */
      z80.bc.b.l = (z80.bc.b.l & ~0x20) |
                   (tape_microphone ? 0x00 : 0x20);

      z80.af.b.l |= 0x01;

      /* Simulate the RET at the end of the edge-finding loop */
      z80.pc.b.l = readbyte_internal( z80.sp.w ); z80.sp.w++;
      z80.pc.b.h = readbyte_internal( z80.sp.w ); z80.sp.w++;
    }

    event_remove_type( tape_edge_event );
    tape_next_edge( tstates, 1 );

    successive_reads = 0;
  }

  length_known1 = length_known2;
  length_long1 = length_long2;
}

static acceleration_mode_t
acceleration_detector( libspectrum_word pc )
{
  int state = 0, count = 0;
  while( 1 ) {
    libspectrum_byte b = readbyte_internal( pc ); pc++; count++;
    switch( state ) {
    case 0:
      switch( b ) {
      case 0x03: state = 28; break;     /* Data byte of JR NZ, ... - Alkatraz */
      case 0x04: state = 1; break;	/* INC B - Many loaders */
      case 0x47: state = 44; break;      /* LD B,A - Software Projects */
      default: state = 13; break;	/* Possible Digital Integration */
      }
      break;
    case 1:
      switch( b ) {
      case 0x20: state = 40; break;     /* JR NZ - variant Alkatraz */
      case 0xc8: state = 2; break;	/* RET Z */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 2:
      switch( b ) {
      case 0x3e: state = 3; break;	/* LD A,nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 3:
      switch( b ) {
      case 0x00:			/* Search Loader */
      case 0x7f:			/* ROM loader and variants */
      case 0xff:                        /* Dinaload */
	state = 4; break;		/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 4:
      switch( b ) {
      case 0x3e: state = 42; break;      /* Second LD A,nn - Microprose */
      case 0xdb: state = 5; break;	/* IN A,(nn) */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 5:
      switch( b ) {
      case 0xfe: state = 6; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 6:
      switch( b ) {
      case 0x1f: state = 7; break;	/* RRA */
      case 0xa9: state = 24; break;	/* XOR C - Search Loader */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 7:
      switch( b ) {
      case 0x00:			/* NOP - Bleepload */
      case 0xa7:			/* AND A - Microsphere */
      case 0xc8:			/* RET Z - Paul Owens */
      case 0xd0:			/* RET NC - ROM loader */
	state = 8; break;
      case 0xa9: state = 9; break;	/* XOR C - Speedlock */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 8:
      switch( b ) {
      case 0xa9: state = 9; break;	/* XOR C */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 9:
      switch( b ) {
      case 0xe6: state = 10; break;	/* AND nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 10:
      switch( b ) {
      case 0x20: state = 11; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 11:
      switch( b ) {
      case 0x28: state = 12; break;	/* JR nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 12:
      if( b == 0x100 - count ) {
	return ACCELERATION_MODE_INCREASING;
      } else {
	return ACCELERATION_MODE_NONE;
      }
      break;

      /* Digital Integration loader */

    case 13:
      state = 14; break;		/* Possible Digital Integration */
    case 14:
      switch( b ) {
      case 0x05: state = 15; break;	/* DEC B - Digital Integration */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 15:
      switch( b ) {
      case 0xc8: state = 16; break;	/* RET Z */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 16:
      switch( b ) {
      case 0xdb: state = 17; break;	/* IN A,(nn) */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 17:
      switch( b ) {
      case 0xfe: state = 18; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 18:
      switch( b ) {
      case 0xa9: state = 19; break;	/* XOR C */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 19:
      switch( b ) {
      case 0xe6: state = 20; break;	/* AND nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 20:
      switch( b ) {
      case 0x40: state = 21; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 21:
      switch( b ) {
      case 0xca: state = 22; break;	/* JP Z,nnnn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 22:				/* LSB of jump target */
      if( b == ( z80.pc.w - 4 ) % 0x100 ) {
	state = 23;
      } else {
	return ACCELERATION_MODE_NONE;
      }
      break;
    case 23:				/* MSB of jump target */
      if( b == ( z80.pc.w - 4 ) / 0x100 ) {
	return ACCELERATION_MODE_DECREASING;
      } else {
	return ACCELERATION_MODE_NONE;
      }

      /* Search loader */

    case 24:
      switch( b ) {
      case 0xe6: state = 25; break;	/* AND nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 25:
      switch( b ) {
      case 0x40: state = 26; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 26:
      switch( b ) {
      case 0x28: state = 12; break;     /* JR Z - Space Crusade */
      case 0xd8: state = 27; break;	/* RET C */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 27:
      switch( b ) {
      case 0x00: state = 11; break;	/* NOP */
      default: return ACCELERATION_MODE_NONE;
      }
      break;

    /* Alkatraz */

    case 28:
      switch( b ) {
      case 0xc3: state = 29; break;     /* JP nnnn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 29:
      state = 30; break;                /* First data byte of JP */
    case 30:
      state = 31; break;                /* Second data byte of JP */
    case 31:
      switch( b ) {
      case 0xdb: state = 32; break;	/* IN A,(nn) */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 32:
      switch( b ) {
      case 0xfe: state = 33; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 33:
      switch( b ) {
      case 0x1f: state = 34; break;	/* RRA */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 34:
      switch( b ) {
      case 0xc8: state = 35; break;	/* RET Z */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 35:
      switch( b ) {
      case 0xa9: state = 36; break;	/* XOR C */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 36:
      switch( b ) {
      case 0xe6: state = 37; break;	/* AND nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 37:
      switch( b ) {
      case 0x20: state = 38; break;	/* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 38:
      switch( b ) {
      case 0x28: state = 39; break;	/* JR Z,nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 39:
      switch( b ) {
      case 0xf1:                        /* Normal data byte */
      case 0xf3:                        /* Variant data byte */
        return ACCELERATION_MODE_INCREASING;
      default: return ACCELERATION_MODE_NONE;
      }
      break;

    /* "Variant" Alkatraz */

    case 40:
      switch( b ) {
      case 0x01: state = 41; break;     /* Data byte of JR NZ */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 41:
      switch( b ) {
      case 0xc9: state = 31; break;     /* RET */
      default: return ACCELERATION_MODE_NONE;
      }
      break;

    /* Microprose */

    case 42:
      switch( b ) {
      case 0x7f: state = 43; break;     /* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 43:
      switch( b ) {
      case 0xdb: state = 5; break;      /* IN A,(nn) */
      default: return ACCELERATION_MODE_NONE;
      }
      break;

    /* Software Projects */

    case 44:
      switch( b ) {
      case 0x08: state = 45; break;      /* EX AF,AF' */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 45:
      switch( b ) {
      case 0x3e: state = 46; break;      /* LD A,nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 46:
      switch( b ) {
      case 0x7f: state = 47; break;      /* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 47:
      switch( b ) {
      case 0xdb: state = 48; break;      /* IN A,(nn) */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 48:
      switch( b ) {
      case 0xfe: state = 49; break;      /* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 49:
      switch( b ) {
      case 0xa9: state = 50; break;      /* XOR C */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 50:
      switch( b ) {
      case 0xe6: state = 51; break;      /* AND nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 51:
      switch( b ) {
      case 0x40: state = 52; break;      /* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 52:
      switch( b ) {
      case 0x20: state = 53; break;      /* JR NZ,nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 53:
      switch( b ) {
      case 0x04: state = 54; break;      /* Data byte */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 54:
      switch( b ) {
      case 0x05: state = 55; break;      /* DEC B */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 55:
      switch( b ) {
      case 0x20: state = 56; break;      /* JR NZ,nn */
      default: return ACCELERATION_MODE_NONE;
      }
      break;
    case 56:
      switch( b ) {
      case 0xf4: return ACCELERATION_MODE_SOFTWARE_PROJECTS;
      default: return ACCELERATION_MODE_NONE;
      }

    default:
      /* Can't happen */
      break;
    }
  }

}      

static acceleration_mode_t
acceleration_detector_at_in( libspectrum_word pc )
{
  acceleration_mode_t mode;

  mode = acceleration_detector( pc - 6 );
  /* Microprose inserts another LD A,0x7f before the IN instruction */
  if( !mode ) mode = acceleration_detector( pc - 8 );

  return mode;
}

int
loader_unittest( void )
{
  static const libspectrum_byte microprose_loader[] = {
    0x04, 0xc8, 0x3e, 0x7f, 0x3e, 0x7f, 0xdb, 0xfe,
    0x1f, 0x00, 0xa9, 0xe6, 0x20, 0x28, 0xf1
  };
  static const libspectrum_byte software_projects_loader[] = {
    0x47, 0x08, 0x3e, 0x7f, 0xdb, 0xfe, 0xa9,
    0xe6, 0x40, 0x20, 0x04, 0x05, 0x20, 0xf4
  };
  const libspectrum_word base = 0x8000;
  libspectrum_byte saved[ sizeof( microprose_loader ) ];
  libspectrum_byte saved_b = z80.bc.b.h, saved_a_ = z80.af_.b.h;
  libspectrum_word saved_pc = z80.pc.w;
  size_t saved_acceleration_pc = acceleration_pc;
  size_t i;
  int error = 0;

  for( i = 0; i < sizeof( microprose_loader ); i++ ) {
    saved[ i ] = readbyte_internal( base + i );
    writebyte_internal( base + i, microprose_loader[ i ] );
  }

  if( acceleration_detector_at_in( base + 8 ) !=
      ACCELERATION_MODE_INCREASING ) error++;

  /* Do not accept an arbitrary second immediate value. */
  writebyte_internal( base + 5, 0x00 );
  if( acceleration_detector_at_in( base + 8 ) != ACCELERATION_MODE_NONE )
    error++;

  for( i = 0; i < sizeof( microprose_loader ); i++ )
    writebyte_internal( base + i, saved[ i ] );

  for( i = 0; i < sizeof( software_projects_loader ); i++ ) {
    saved[ i ] = readbyte_internal( base + i );
    writebyte_internal( base + i, software_projects_loader[ i ] );
  }

  if( acceleration_detector_at_in( base + 6 ) !=
      ACCELERATION_MODE_SOFTWARE_PROJECTS ) error++;

  /* The branch must return to the start of this specific sampling loop. */
  writebyte_internal( base + 13, 0xf5 );
  if( acceleration_detector_at_in( base + 6 ) != ACCELERATION_MODE_NONE )
    error++;
  writebyte_internal( base + 13, software_projects_loader[ 13 ] );

  acceleration_pc = base + 6;
  z80.af_.b.h = 0x24;
  software_projects_accelerate( 0 );
  if( z80.bc.b.h != 0x1a || z80.pc.w != base + 15 ) error++;
  software_projects_accelerate( 1 );
  if( z80.bc.b.h != 0x10 || z80.pc.w != base + 15 ) error++;

  for( i = 0; i < sizeof( software_projects_loader ); i++ )
    writebyte_internal( base + i, saved[ i ] );
  z80.bc.b.h = saved_b; z80.af_.b.h = saved_a_; z80.pc.w = saved_pc;
  acceleration_pc = saved_acceleration_pc;

  if( error ) printf( "loader_unittest failed\n" );
  return error;
}

static void
check_for_acceleration( void )
{
  /* If the IN occured at a different location to the one we're
     accelerating, stop acceleration */
  if( acceleration_mode && z80.pc.w != acceleration_pc )
    acceleration_mode = ACCELERATION_MODE_NONE;

  /* If we're not accelerating, check if this is a loader */
  if( !acceleration_mode ) {
    acceleration_mode = acceleration_detector_at_in( z80.pc.w );
    acceleration_pc = z80.pc.w;
  }

  if( acceleration_mode ) do_acceleration();
}

void
loader_detect_loader( void )
{
  libspectrum_dword tstates_diff = tstates - last_tstates_read;
  libspectrum_byte b_diff = z80.bc.b.h - last_b_read;

  last_tstates_read = tstates;
  last_b_read = z80.bc.b.h;

  if( settings_current.detect_loader ) {

    if( tape_is_playing() ) {
      if( tstates_diff > 1000 || ( b_diff != 1 && b_diff != 0 &&
				   b_diff != 0xff ) ) {
	successive_reads++;
	if( successive_reads >= 2 ) {
	  tape_stop();
	}
      } else {
	successive_reads = 0;
      }
    } else {
      if( tstates_diff <= 500 && ( b_diff == 1 || b_diff == 0xff ) ) {
	successive_reads++;
	if( successive_reads >= 10 ) {
	  tape_do_play( 1 );
	}
      } else {
	successive_reads = 0;
      }
    }

  } else {

    successive_reads = 0;

  }

  if( settings_current.accelerate_loader && tape_is_playing() &&
      !rzx_recording )
    check_for_acceleration();

}

void
loader_set_acceleration_flags( int flags, int from_acceleration )
{
  if( flags & LIBSPECTRUM_TAPE_FLAGS_LENGTH_SHORT ) {
    length_known2 = 1;
    length_long2 = 0;
  } else if( flags & LIBSPECTRUM_TAPE_FLAGS_LENGTH_LONG ) {
    length_known2 = 1;
    length_long2 = 1;
  } else {
    length_known2 = 0;
  }

  /* If this tape edge occurred due to normal timings rather than
     our tape acceleration, turn off acceleration for the next edge
     or we miss an edge. See [bugs:#387] for more details */
  if( !from_acceleration ) {
    length_known1 = 0;
  }
}
