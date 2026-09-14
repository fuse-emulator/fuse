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
  ACCELERATION_MODE_GREMLIN_RISING,
  ACCELERATION_MODE_GREMLIN_FALLING,
} acceleration_mode_t;

static acceleration_mode_t acceleration_mode;
static size_t acceleration_pc;

#define SOFTWARE_PROJECTS_SHORT_PULSE_ITERATIONS 10
#define SOFTWARE_PROJECTS_LONG_PULSE_ITERATIONS 20

/* Gremlin counts both halves of a double pulse in L. Each sampling loop takes
   29 T-states, giving approximately 24 or 48 iterations per tape pulse. */
#define GREMLIN_SHORT_PULSE_ITERATIONS 24
#define GREMLIN_LONG_PULSE_ITERATIONS 48

/* Movieload needs time to settle after the previous block before playback is
   restarted. Starting on the first recognised read corrupts the load. */
#define MOVIELOAD_DETECTION_READS 128
#define LOADER_STOP_NON_EAR_READS 10

/* A zero mask ignores a byte; other masks allow closely related opcodes to
   share a pattern without adding control flow to the detector. */
typedef struct loader_pattern_byte_t {
  libspectrum_byte value;
  libspectrum_byte mask;
} loader_pattern_byte_t;

#define LOADER_PATTERN_BYTE( value ) { value, 0xff }
#define LOADER_PATTERN_MASKED( value, mask ) { value, mask }
#define LOADER_PATTERN_ANY { 0x00, 0x00 }

static int
loader_pattern_matches( libspectrum_word address,
                        const loader_pattern_byte_t *pattern, size_t length )
{
  for( size_t i = 0; i < length; i++ )
    if( ( readbyte_internal( address + i ) & pattern[ i ].mask ) !=
        pattern[ i ].value ) return 0;

  return 1;
}

static int
loader_word_matches( libspectrum_word address, libspectrum_word value )
{
  return readbyte_internal( address ) == value % 0x100 &&
         readbyte_internal( address + 1 ) == value / 0x100;
}

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
gremlin_accelerate( int long_pulse )
{
  /* INC L has already executed once before loader_detect_loader(). */
  z80.hl.b.l += ( long_pulse ? GREMLIN_LONG_PULSE_ITERATIONS :
                                GREMLIN_SHORT_PULSE_ITERATIONS ) - 1;

  if( acceleration_mode == ACCELERATION_MODE_GREMLIN_RISING ) {
    /* Continue with the OUT and falling-edge loop. */
    z80.pc.w = acceleration_pc + 4;
  } else {
    /* The routine returns the combined rising/falling count in A. */
    z80.af.b.h = z80.hl.b.l;
    z80.pc.w = acceleration_pc + 4;
  }
}

static void
do_acceleration( void )
{
  if( length_known1 ) {
    if( acceleration_mode == ACCELERATION_MODE_SOFTWARE_PROJECTS ) {
      /* The loader converts the number of loop iterations to a pulse length
         by subtracting B from A' and multiplying the result by four. */
      software_projects_accelerate( length_long1 );
    } else if( acceleration_mode == ACCELERATION_MODE_GREMLIN_RISING ||
               acceleration_mode == ACCELERATION_MODE_GREMLIN_FALLING ) {
      gremlin_accelerate( length_long1 );
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
gremlin_acceleration_detector( libspectrum_word pc )
{
  /* Rising edge: LD L,0; INC L; IN A,(FE); AND H; JP Z,<INC L>. */
  if( readbyte_internal( pc - 5 ) == 0x2e &&
      readbyte_internal( pc - 4 ) == 0x00 &&
      readbyte_internal( pc - 3 ) == 0x2c &&
      readbyte_internal( pc - 2 ) == 0xdb &&
      readbyte_internal( pc - 1 ) == 0xfe &&
      readbyte_internal( pc ) == 0xa4 &&
      readbyte_internal( pc + 1 ) == 0xca &&
      readbyte_internal( pc + 2 ) == ( pc - 3 ) % 0x100 &&
      readbyte_internal( pc + 3 ) == ( pc - 3 ) / 0x100 )
    return ACCELERATION_MODE_GREMLIN_RISING;

  /* Falling edge: LD A,8; OUT (FE),A; INC L; IN A,(FE); AND H;
     JP NZ,<INC L>. */
  if( readbyte_internal( pc - 7 ) == 0x3e &&
      readbyte_internal( pc - 6 ) == 0x08 &&
      readbyte_internal( pc - 5 ) == 0xd3 &&
      readbyte_internal( pc - 4 ) == 0xfe &&
      readbyte_internal( pc - 3 ) == 0x2c &&
      readbyte_internal( pc - 2 ) == 0xdb &&
      readbyte_internal( pc - 1 ) == 0xfe &&
      readbyte_internal( pc ) == 0xa4 &&
      readbyte_internal( pc + 1 ) == 0xc2 &&
      readbyte_internal( pc + 2 ) == ( pc - 3 ) % 0x100 &&
      readbyte_internal( pc + 3 ) == ( pc - 3 ) / 0x100 )
    return ACCELERATION_MODE_GREMLIN_FALLING;

  return ACCELERATION_MODE_NONE;
}

static int
modified_rom_loader_detector( libspectrum_word pc )
{
  libspectrum_word callback;

  /* Technician Ted replaces the ROM EDGE1 delay with useful work, entered
     with EXX and left immediately before the copied EDGE2 sampling loop. The
     work and the real sampling delay form one timing unit, so short-circuiting
     only EDGE2 corrupts the load. Keep the loop as strong autoplay evidence,
     but do not accelerate it. */
  callback = readbyte_internal( pc - 30 ) |
             readbyte_internal( pc - 29 ) << 8;

  return readbyte_internal( pc - 40 ) == 0xd9 && /* EXX */
         readbyte_internal( pc - 39 ) == 0x11 && /* LD DE,nn */
         readbyte_internal( pc - 36 ) == 0x21 && /* LD HL,nn */
         readbyte_internal( pc - 33 ) == 0x06 && /* LD B,5 */
         readbyte_internal( pc - 32 ) == 0x05 &&
         readbyte_internal( pc - 31 ) == 0xcd && /* CALL callback */
         callback == ( pc - 24 ) % 0x10000 &&
         readbyte_internal( pc - 28 ) == 0xd0 && /* RET NC */
         readbyte_internal( pc - 27 ) == 0xd9 && /* EXX */
         readbyte_internal( pc - 8 ) == 0xd9 &&  /* EXX */
         readbyte_internal( pc - 7 ) == 0xa7 &&  /* AND A */
         acceleration_detector( pc - 6 ) == ACCELERATION_MODE_INCREASING;
}

static acceleration_mode_t
acceleration_detector_at_in( libspectrum_word pc )
{
  acceleration_mode_t mode;

  if( modified_rom_loader_detector( pc ) ) return ACCELERATION_MODE_NONE;

  mode = gremlin_acceleration_detector( pc );
  if( mode ) return mode;

  mode = acceleration_detector( pc - 6 );
  /* Microprose inserts another LD A,0x7f before the IN instruction */
  if( !mode ) mode = acceleration_detector( pc - 8 );

  return mode;
}

static int
movieload_loader_detector( libspectrum_word pc )
{
  /* INC D; RET Z; LD A,7f; IN A,(fe); RRA; NOP; XOR E;
     AND 20; JR Z,<INC D>. Movieload uses D rather than B as its counter. */
  return readbyte_internal( pc - 6 ) == 0x14 &&
         readbyte_internal( pc - 5 ) == 0xc8 &&
         readbyte_internal( pc - 4 ) == 0x3e &&
         readbyte_internal( pc - 3 ) == 0x7f &&
         readbyte_internal( pc - 2 ) == 0xdb &&
         readbyte_internal( pc - 1 ) == 0xfe &&
         readbyte_internal( pc ) == 0x1f &&
         readbyte_internal( pc + 1 ) == 0x00 &&
         readbyte_internal( pc + 2 ) == 0xab &&
         readbyte_internal( pc + 3 ) == 0xe6 &&
         readbyte_internal( pc + 4 ) == 0x20 &&
         readbyte_internal( pc + 5 ) == 0x28 &&
         readbyte_internal( pc + 6 ) == 0xf3;
}

static int
sign_flag_loader_detector( libspectrum_word pc )
{
  libspectrum_byte jump = readbyte_internal( pc + 1 );

  /* INC B; RET Z; IN A,(fe); ADD A,A; JP P/M,<INC B>. The loader tests
     EAR by moving bit 6 into the sign flag. */
  return readbyte_internal( pc - 4 ) == 0x04 &&
         readbyte_internal( pc - 3 ) == 0xc8 &&
         readbyte_internal( pc - 2 ) == 0xdb &&
         readbyte_internal( pc - 1 ) == 0xfe &&
         readbyte_internal( pc ) == 0x87 &&
         ( jump == 0xf2 || jump == 0xfa ) &&
         readbyte_internal( pc + 2 ) == ( pc - 4 ) % 0x100 &&
         readbyte_internal( pc + 3 ) == ( pc - 4 ) / 0x100;
}

static int
loader_loop_detector( libspectrum_word pc )
{
  return modified_rom_loader_detector( pc ) ||
         acceleration_detector_at_in( pc ) != ACCELERATION_MODE_NONE ||
         sign_flag_loader_detector( pc );
}

static int
ula_read_uses_ear( libspectrum_word pc )
{
  int i;

  /* A loader normally either tests EAR directly with bit 6, or rotates it
     into bit 5 first. Requiring that use prevents keyboard scans from
     satisfying the fallback timing heuristic. */
  if( readbyte_internal( pc ) == 0x1f ) {   /* RRA */
    for( i = 1; i < 6; i++ )
      if( readbyte_internal( pc + i ) == 0xe6 &&
          readbyte_internal( pc + i + 1 ) == 0x20 ) return 1;
  }

  for( i = 0; i < 5; i++ )
    if( readbyte_internal( pc + i ) == 0xe6 &&
        readbyte_internal( pc + i + 1 ) == 0x40 ) return 1;

  return 0;
}

#define LOADER_TEST_BASE 0x8000
#define LOADER_TEST_MAX_LENGTH 50

typedef struct loader_test_memory_t {
  libspectrum_byte saved[ LOADER_TEST_MAX_LENGTH ];
  size_t length;
} loader_test_memory_t;

typedef struct loader_test_state_t {
  libspectrum_byte b, a_, a, l;
  libspectrum_word pc;
  size_t acceleration_pc;
  acceleration_mode_t acceleration_mode;
} loader_test_state_t;

static void
loader_test_install( loader_test_memory_t *memory,
                     const libspectrum_byte *bytes, size_t length )
{
  size_t i;

  memory->length = length;
  for( i = 0; i < length; i++ ) {
    memory->saved[ i ] = readbyte_internal( LOADER_TEST_BASE + i );
    writebyte_internal( LOADER_TEST_BASE + i, bytes ? bytes[ i ] : 0x00 );
  }
}

static void
loader_test_restore_memory( const loader_test_memory_t *memory )
{
  size_t i;

  for( i = 0; i < memory->length; i++ )
    writebyte_internal( LOADER_TEST_BASE + i, memory->saved[ i ] );
}

static int
loader_test_pattern_matcher( void )
{
  static const libspectrum_byte bytes[] = { 0xaa, 0x31, 0xfa, 0xcc };
  static const loader_pattern_byte_t pattern[] = {
    LOADER_PATTERN_BYTE( 0xaa ),
    LOADER_PATTERN_ANY,
    /* JP P (f2) and JP M (fa) differ only in bit 3. */
    LOADER_PATTERN_MASKED( 0xf2, 0xf7 ),
    LOADER_PATTERN_BYTE( 0xcc ),
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, bytes, sizeof( bytes ) );
  if( !loader_pattern_matches( LOADER_TEST_BASE, pattern,
                               sizeof( pattern ) / sizeof( pattern[ 0 ] ) ) )
    error++;

  writebyte_internal( LOADER_TEST_BASE, 0xab );
  if( loader_pattern_matches( LOADER_TEST_BASE, pattern,
                              sizeof( pattern ) / sizeof( pattern[ 0 ] ) ) )
    error++;

  writebyte_internal( LOADER_TEST_BASE, 0x34 );
  writebyte_internal( LOADER_TEST_BASE + 1, 0x92 );
  if( !loader_word_matches( LOADER_TEST_BASE, 0x9234 ) ) error++;
  if( loader_word_matches( LOADER_TEST_BASE, 0x9235 ) ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_microprose( void )
{
  static const libspectrum_byte loader[] = {
    0x04, 0xc8, 0x3e, 0x7f, 0x3e, 0x7f, 0xdb, 0xfe,
    0x1f, 0x00, 0xa9, 0xe6, 0x20, 0x28, 0xf1
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 8 ) !=
      ACCELERATION_MODE_INCREASING ) error++;

  /* Do not accept an arbitrary second immediate value. */
  writebyte_internal( LOADER_TEST_BASE + 5, 0x00 );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 8 ) !=
      ACCELERATION_MODE_NONE ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_software_projects( void )
{
  static const libspectrum_byte loader[] = {
    0x47, 0x08, 0x3e, 0x7f, 0xdb, 0xfe, 0xa9,
    0xe6, 0x40, 0x20, 0x04, 0x05, 0x20, 0xf4
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 6 ) !=
      ACCELERATION_MODE_SOFTWARE_PROJECTS ) error++;

  /* The branch must return to the start of this specific sampling loop. */
  writebyte_internal( LOADER_TEST_BASE + 13, 0xf5 );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 6 ) !=
      ACCELERATION_MODE_NONE ) error++;
  writebyte_internal( LOADER_TEST_BASE + 13, loader[ 13 ] );

  acceleration_pc = LOADER_TEST_BASE + 6;
  z80.af_.b.h = 0x24;
  software_projects_accelerate( 0 );
  if( z80.bc.b.h != 0x1a || z80.pc.w != LOADER_TEST_BASE + 15 ) error++;
  software_projects_accelerate( 1 );
  if( z80.bc.b.h != 0x10 || z80.pc.w != LOADER_TEST_BASE + 15 ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_gremlin( void )
{
  static const libspectrum_byte loader[] = {
    0x2e, 0x00, 0x2c, 0xdb, 0xfe, 0xa4, 0xca, 0x02, 0x80,
    0x3e, 0x08, 0xd3, 0xfe, 0x2c, 0xdb, 0xfe, 0xa4, 0xc2, 0x0d, 0x80,
    0x7d, 0xc9
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 5 ) !=
      ACCELERATION_MODE_GREMLIN_RISING ) error++;
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 16 ) !=
      ACCELERATION_MODE_GREMLIN_FALLING ) error++;

  /* Do not accept a falling-edge branch to a different loop. */
  writebyte_internal( LOADER_TEST_BASE + 18, 0x0c );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 16 ) !=
      ACCELERATION_MODE_NONE ) error++;
  writebyte_internal( LOADER_TEST_BASE + 18, loader[ 18 ] );

  z80.hl.b.l = 1;             /* First INC L has executed. */
  acceleration_mode = ACCELERATION_MODE_GREMLIN_RISING;
  acceleration_pc = LOADER_TEST_BASE + 5;
  gremlin_accelerate( 0 );
  if( z80.hl.b.l != 24 || z80.pc.w != LOADER_TEST_BASE + 9 ) error++;
  z80.hl.b.l++;                /* INC L in the falling-edge loop. */
  acceleration_mode = ACCELERATION_MODE_GREMLIN_FALLING;
  acceleration_pc = LOADER_TEST_BASE + 16;
  gremlin_accelerate( 0 );
  if( z80.hl.b.l != 48 || z80.af.b.h != 48 ||
      z80.pc.w != LOADER_TEST_BASE + 20 ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_ear_use( void )
{
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, NULL, 22 );
  /* A keyboard scan's AND 1f does not consume the EAR input. */
  writebyte_internal( LOADER_TEST_BASE + 5, 0xdb );
  writebyte_internal( LOADER_TEST_BASE + 6, 0xfe );
  writebyte_internal( LOADER_TEST_BASE + 7, 0x2f );
  writebyte_internal( LOADER_TEST_BASE + 8, 0xe6 );
  writebyte_internal( LOADER_TEST_BASE + 9, 0x1f );
  if( ula_read_uses_ear( LOADER_TEST_BASE + 7 ) ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_movieload( void )
{
  static const libspectrum_byte loader[] = {
    0x14, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f,
    0x00, 0xab, 0xe6, 0x20, 0x28, 0xf3
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( !movieload_loader_detector( LOADER_TEST_BASE + 6 ) ) error++;

  /* The branch must return to this sampling loop. */
  writebyte_internal( LOADER_TEST_BASE + 12, 0xf2 );
  if( movieload_loader_detector( LOADER_TEST_BASE + 6 ) ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_sign_flag( void )
{
  static const libspectrum_byte loader[] = {
    0x04, 0xc8, 0xdb, 0xfe, 0x87, 0xfa, 0x00, 0x80
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( !sign_flag_loader_detector( LOADER_TEST_BASE + 4 ) ) error++;
  writebyte_internal( LOADER_TEST_BASE + 6, 0x01 );
  if( sign_flag_loader_detector( LOADER_TEST_BASE + 4 ) ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static int
loader_test_modified_rom( void )
{
  static const libspectrum_byte loader[] = {
    0xd9, 0x11, 0x6b, 0x80, 0x21, 0x29, 0x80, 0x06,
    0x05, 0xcd, 0x10, 0x80, 0xd0, 0xd9, 0x06, 0x06,
    0x7e, 0x91, 0x77, 0x7d, 0x38, 0x03, 0x00, 0x18,
    0x02, 0x12, 0x1c, 0xc6, 0x06, 0x6f, 0x10, 0xf0,
    0xd9, 0xa7, 0x04, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe,
    0x1f, 0xd0, 0xa9, 0xe6, 0x20, 0x28, 0xf3, 0x79,
    0x2f, 0x4f, 0x37, 0xc9
  };
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  if( !modified_rom_loader_detector( LOADER_TEST_BASE + 40 ) ) error++;
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 40 ) !=
      ACCELERATION_MODE_NONE ) error++;
  if( !loader_loop_detector( LOADER_TEST_BASE + 40 ) ) error++;

  /* A call elsewhere is not the custom EDGE1 timing context. */
  writebyte_internal( LOADER_TEST_BASE + 10, 0x11 );
  if( modified_rom_loader_detector( LOADER_TEST_BASE + 40 ) ) error++;
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 40 ) !=
      ACCELERATION_MODE_INCREASING ) error++;

  loader_test_restore_memory( &memory );
  return error;
}

static loader_test_state_t
loader_test_save_state( void )
{
  loader_test_state_t state;

  state.b = z80.bc.b.h;
  state.a_ = z80.af_.b.h;
  state.a = z80.af.b.h;
  state.l = z80.hl.b.l;
  state.pc = z80.pc.w;
  state.acceleration_pc = acceleration_pc;
  state.acceleration_mode = acceleration_mode;
  return state;
}

static void
loader_test_restore_state( const loader_test_state_t *state )
{
  z80.bc.b.h = state->b;
  z80.af_.b.h = state->a_;
  z80.af.b.h = state->a;
  z80.hl.b.l = state->l;
  z80.pc.w = state->pc;
  acceleration_pc = state->acceleration_pc;
  acceleration_mode = state->acceleration_mode;
}

int
loader_unittest( void )
{
  loader_test_state_t state = loader_test_save_state();
  int error = 0;

  error += loader_test_pattern_matcher();
  error += loader_test_microprose();
  error += loader_test_software_projects();
  error += loader_test_gremlin();
  error += loader_test_ear_use();
  error += loader_test_movieload();
  error += loader_test_sign_flag();
  error += loader_test_modified_rom();

  loader_test_restore_state( &state );
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
      if( loader_loop_detector( z80.pc.w ) ||
          movieload_loader_detector( z80.pc.w ) ||
          ula_read_uses_ear( z80.pc.w ) ) {
        successive_reads = 0;
      } else if( tstates_diff > 1000 ||
                 ( b_diff != 1 && b_diff != 0 && b_diff != 0xff ) ) {
	successive_reads++;
	/* A loader may be interrupted by an eight-read keyboard scan. Do not
           stop the tape unless non-EAR reads persist beyond that interrupt. */
	if( successive_reads >= LOADER_STOP_NON_EAR_READS ) tape_stop();
      } else {
	successive_reads = 0;
      }
    } else {
      if( movieload_loader_detector( z80.pc.w ) && tstates_diff <= 500 ) {
        /* Unlike the other recognised loops, Movieload must sample the idle
           input for a while before playback starts. */
        successive_reads++;
        if( successive_reads >= MOVIELOAD_DETECTION_READS ) tape_do_play( 1 );
      } else if( loader_loop_detector( z80.pc.w ) ) {
        /* An instruction-level match also covers loaders which count outside
           B. Wait for repeated reads so playback starts between samples, as
           it does with the timing heuristic. */
        successive_reads++;
        if( successive_reads >= 10 ) tape_do_play( 1 );
      } else if( ula_read_uses_ear( z80.pc.w ) && tstates_diff <= 500 &&
                 ( b_diff == 1 || b_diff == 0xff ) ) {
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
