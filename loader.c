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

#define LOADER_PATTERN_MAX_ALTERNATIVES 4

typedef struct loader_pattern_byte_t {
  libspectrum_byte values[ LOADER_PATTERN_MAX_ALTERNATIVES ];
  libspectrum_byte masks[ LOADER_PATTERN_MAX_ALTERNATIVES ];
  size_t alternatives;
} loader_pattern_byte_t;

#define LOADER_PATTERN_BYTE( value ) \
  { .values = { value }, .masks = { 0xff }, .alternatives = 1 }
#define LOADER_PATTERN_MASKED( value, mask ) \
  { .values = { value }, .masks = { mask }, .alternatives = 1 }
#define LOADER_PATTERN_ANY { .alternatives = 0 }
#define LOADER_PATTERN_ONE_OF_2( a, b ) \
  { .values = { a, b }, .masks = { 0xff, 0xff }, .alternatives = 2 }
#define LOADER_PATTERN_ONE_OF_3( a, b, c ) \
  { .values = { a, b, c }, .masks = { 0xff, 0xff, 0xff }, \
    .alternatives = 3 }
#define LOADER_PATTERN_ONE_OF_4( a, b, c, d ) \
  { .values = { a, b, c, d }, .masks = { 0xff, 0xff, 0xff, 0xff }, \
    .alternatives = 4 }

static int
loader_pattern_byte_matches( libspectrum_byte byte,
                             const loader_pattern_byte_t *pattern )
{
  if( !pattern->alternatives ) return 1;

  for( size_t i = 0; i < pattern->alternatives; i++ )
    if( ( byte & pattern->masks[ i ] ) == pattern->values[ i ] ) return 1;

  return 0;
}

static int
loader_pattern_matches( libspectrum_word address,
                        const loader_pattern_byte_t *pattern, size_t length )
{
  for( size_t i = 0; i < length; i++ )
    if( !loader_pattern_byte_matches( readbyte_internal( address + i ),
                                      &pattern[ i ] ) ) return 0;

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
rom_loader_accelerate( int long_pulse )
{
  /* B is used to indicate the length of the pulses. */
  int set_b_high = long_pulse ^
                   ( acceleration_mode == ACCELERATION_MODE_DECREASING );
  z80.bc.b.h = set_b_high ? 0xfe : 0x00;

  /* Bit 5 of C is used to indicate the current microphone level. */
  z80.bc.b.l = ( z80.bc.b.l & ~0x20 ) |
               ( tape_microphone ? 0x00 : 0x20 );

  z80.af.b.l |= 0x01;

  /* Simulate the RET at the end of the edge-finding loop. */
  z80.pc.b.l = readbyte_internal( z80.sp.w ); z80.sp.w++;
  z80.pc.b.h = readbyte_internal( z80.sp.w ); z80.sp.w++;
}

static void
accelerate_loader( int long_pulse )
{
  switch( acceleration_mode ) {
  case ACCELERATION_MODE_SOFTWARE_PROJECTS:
    /* The loader converts the number of loop iterations to a pulse length
       by subtracting B from A' and multiplying the result by four. */
    software_projects_accelerate( long_pulse );
    break;
  case ACCELERATION_MODE_GREMLIN_RISING:
  case ACCELERATION_MODE_GREMLIN_FALLING:
    gremlin_accelerate( long_pulse );
    break;
  case ACCELERATION_MODE_INCREASING:
  case ACCELERATION_MODE_DECREASING:
    rom_loader_accelerate( long_pulse );
    break;
  case ACCELERATION_MODE_NONE:
    break;
  }
}

static void
do_acceleration( void )
{
  if( length_known1 ) {
    accelerate_loader( length_long1 );
    event_remove_type( tape_edge_event );
    tape_next_edge( tstates, 1 );
    successive_reads = 0;
  }

  length_known1 = length_known2;
  length_long1 = length_long2;
}

#define LOADER_PATTERN_LENGTH( pattern ) \
  ( sizeof( pattern ) / sizeof( pattern[ 0 ] ) )

#define ROM_LOADER_PREFIX \
  LOADER_PATTERN_BYTE( 0x04 ), LOADER_PATTERN_BYTE( 0xc8 ), \
  LOADER_PATTERN_BYTE( 0x3e ), \
  LOADER_PATTERN_ONE_OF_3( 0x00, 0x7f, 0xff )
#define ROM_LOADER_INPUT \
  LOADER_PATTERN_BYTE( 0xdb ), LOADER_PATTERN_BYTE( 0xfe )
#define ROM_LOADER_EDGE_TEST \
  LOADER_PATTERN_BYTE( 0x1f )
#define ROM_LOADER_LEVEL_TEST \
  LOADER_PATTERN_BYTE( 0xa9 ), LOADER_PATTERN_BYTE( 0xe6 ), \
  LOADER_PATTERN_BYTE( 0x20 ), LOADER_PATTERN_BYTE( 0x28 )
#define ROM_LOADER_EDGE_ACTION \
  LOADER_PATTERN_ONE_OF_4( 0x00, 0xa7, 0xc8, 0xd0 )

static const loader_pattern_byte_t rom_loader[] = {
  ROM_LOADER_PREFIX, ROM_LOADER_INPUT, ROM_LOADER_EDGE_TEST,
  ROM_LOADER_LEVEL_TEST, LOADER_PATTERN_BYTE( 0xf4 ),
};

static const loader_pattern_byte_t rom_loader_with_edge_action[] = {
  ROM_LOADER_PREFIX, ROM_LOADER_INPUT, ROM_LOADER_EDGE_TEST,
  ROM_LOADER_EDGE_ACTION, ROM_LOADER_LEVEL_TEST, LOADER_PATTERN_BYTE( 0xf3 ),
};

#define MICROPROSE_PREFIX \
  ROM_LOADER_PREFIX, LOADER_PATTERN_BYTE( 0x3e ), \
  LOADER_PATTERN_BYTE( 0x7f ), ROM_LOADER_INPUT, ROM_LOADER_EDGE_TEST

static const loader_pattern_byte_t microprose_loader[] = {
  MICROPROSE_PREFIX, ROM_LOADER_LEVEL_TEST, LOADER_PATTERN_BYTE( 0xf2 ),
};

static const loader_pattern_byte_t microprose_loader_with_edge_action[] = {
  MICROPROSE_PREFIX, ROM_LOADER_EDGE_ACTION, ROM_LOADER_LEVEL_TEST,
  LOADER_PATTERN_BYTE( 0xf1 ),
};

static const loader_pattern_byte_t search_loader[] = {
  ROM_LOADER_PREFIX, ROM_LOADER_INPUT, LOADER_PATTERN_BYTE( 0xa9 ),
  LOADER_PATTERN_BYTE( 0xe6 ), LOADER_PATTERN_BYTE( 0x40 ),
  LOADER_PATTERN_BYTE( 0x28 ), LOADER_PATTERN_BYTE( 0xf5 ),
};

static const loader_pattern_byte_t search_loader_with_ret[] = {
  ROM_LOADER_PREFIX, ROM_LOADER_INPUT, LOADER_PATTERN_BYTE( 0xa9 ),
  LOADER_PATTERN_BYTE( 0xe6 ), LOADER_PATTERN_BYTE( 0x40 ),
  LOADER_PATTERN_BYTE( 0xd8 ), LOADER_PATTERN_BYTE( 0x00 ),
  LOADER_PATTERN_BYTE( 0x28 ), LOADER_PATTERN_BYTE( 0xf3 ),
};

#define ALKATRAZ_SUFFIX \
  ROM_LOADER_INPUT, ROM_LOADER_EDGE_TEST, LOADER_PATTERN_BYTE( 0xc8 ), \
  ROM_LOADER_LEVEL_TEST, LOADER_PATTERN_ONE_OF_2( 0xf1, 0xf3 )

static const loader_pattern_byte_t alkatraz_loader[] = {
  LOADER_PATTERN_BYTE( 0x03 ), LOADER_PATTERN_BYTE( 0xc3 ),
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY, ALKATRAZ_SUFFIX,
};

static const loader_pattern_byte_t alkatraz_variant_loader[] = {
  LOADER_PATTERN_BYTE( 0x04 ), LOADER_PATTERN_BYTE( 0x20 ),
  LOADER_PATTERN_BYTE( 0x01 ), LOADER_PATTERN_BYTE( 0xc9 ),
  ALKATRAZ_SUFFIX,
};

static const loader_pattern_byte_t software_projects_loader[] = {
  LOADER_PATTERN_BYTE( 0x47 ), LOADER_PATTERN_BYTE( 0x08 ),
  LOADER_PATTERN_BYTE( 0x3e ), LOADER_PATTERN_BYTE( 0x7f ),
  ROM_LOADER_INPUT, LOADER_PATTERN_BYTE( 0xa9 ),
  LOADER_PATTERN_BYTE( 0xe6 ), LOADER_PATTERN_BYTE( 0x40 ),
  LOADER_PATTERN_BYTE( 0x20 ), LOADER_PATTERN_BYTE( 0x04 ),
  LOADER_PATTERN_BYTE( 0x05 ), LOADER_PATTERN_BYTE( 0x20 ),
  LOADER_PATTERN_BYTE( 0xf4 ),
};

static const loader_pattern_byte_t digital_integration_loader[] = {
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY, LOADER_PATTERN_BYTE( 0x05 ),
  LOADER_PATTERN_BYTE( 0xc8 ), ROM_LOADER_INPUT,
  LOADER_PATTERN_BYTE( 0xa9 ), LOADER_PATTERN_BYTE( 0xe6 ),
  LOADER_PATTERN_BYTE( 0x40 ), LOADER_PATTERN_BYTE( 0xca ),
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
};

typedef struct acceleration_pattern_t {
  const loader_pattern_byte_t *bytes;
  size_t length;
  acceleration_mode_t mode;
} acceleration_pattern_t;

#define ACCELERATION_PATTERN( pattern, acceleration_mode ) \
  { pattern, LOADER_PATTERN_LENGTH( pattern ), acceleration_mode }

static const acceleration_pattern_t acceleration_patterns[] = {
  ACCELERATION_PATTERN( rom_loader, ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( rom_loader_with_edge_action,
                        ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( microprose_loader, ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( microprose_loader_with_edge_action,
                        ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( search_loader, ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( search_loader_with_ret,
                        ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( alkatraz_loader, ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( alkatraz_variant_loader,
                        ACCELERATION_MODE_INCREASING ),
  ACCELERATION_PATTERN( software_projects_loader,
                        ACCELERATION_MODE_SOFTWARE_PROJECTS ),
};

static int
digital_integration_loader_matches( libspectrum_word pc )
{
  libspectrum_byte first = readbyte_internal( pc );

  /* These bytes select other state-machine branches in the original
     detector, so they cannot begin a Digital Integration signature. */
  if( first == 0x03 || first == 0x04 || first == 0x47 ) return 0;

  if( !loader_pattern_matches( pc, digital_integration_loader,
                               LOADER_PATTERN_LENGTH(
                                 digital_integration_loader ) ) ) return 0;

  return loader_word_matches( pc + 10, z80.pc.w - 4 );
}

static acceleration_mode_t
acceleration_detector( libspectrum_word pc )
{
  if( digital_integration_loader_matches( pc ) )
    return ACCELERATION_MODE_DECREASING;

  for( size_t i = 0; i < LOADER_PATTERN_LENGTH( acceleration_patterns ); i++ )
    if( loader_pattern_matches( pc, acceleration_patterns[ i ].bytes,
                                acceleration_patterns[ i ].length ) )
      return acceleration_patterns[ i ].mode;

  return ACCELERATION_MODE_NONE;
}

static const loader_pattern_byte_t gremlin_rising_loader[] = {
  LOADER_PATTERN_BYTE( 0x2e ), LOADER_PATTERN_BYTE( 0x00 ),
  LOADER_PATTERN_BYTE( 0x2c ), ROM_LOADER_INPUT,
  LOADER_PATTERN_BYTE( 0xa4 ), LOADER_PATTERN_BYTE( 0xca ),
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
};

static const loader_pattern_byte_t gremlin_falling_loader[] = {
  LOADER_PATTERN_BYTE( 0x3e ), LOADER_PATTERN_BYTE( 0x08 ),
  LOADER_PATTERN_BYTE( 0xd3 ), LOADER_PATTERN_BYTE( 0xfe ),
  LOADER_PATTERN_BYTE( 0x2c ), ROM_LOADER_INPUT,
  LOADER_PATTERN_BYTE( 0xa4 ), LOADER_PATTERN_BYTE( 0xc2 ),
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
};

static acceleration_mode_t
gremlin_acceleration_detector( libspectrum_word pc )
{
  /* Rising edge: LD L,0; INC L; IN A,(FE); AND H; JP Z,<INC L>. */
  if( loader_pattern_matches( pc - 5, gremlin_rising_loader,
                              LOADER_PATTERN_LENGTH(
                                gremlin_rising_loader ) ) &&
      loader_word_matches( pc + 2, pc - 3 ) )
    return ACCELERATION_MODE_GREMLIN_RISING;

  /* Falling edge: LD A,8; OUT (FE),A; INC L; IN A,(FE); AND H;
     JP NZ,<INC L>. */
  if( loader_pattern_matches( pc - 7, gremlin_falling_loader,
                              LOADER_PATTERN_LENGTH(
                                gremlin_falling_loader ) ) &&
      loader_word_matches( pc + 2, pc - 3 ) )
    return ACCELERATION_MODE_GREMLIN_FALLING;

  return ACCELERATION_MODE_NONE;
}

static const loader_pattern_byte_t modified_rom_prefix[] = {
  LOADER_PATTERN_BYTE( 0xd9 ),             /* EXX */
  LOADER_PATTERN_BYTE( 0x11 ),             /* LD DE,nn */
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
  LOADER_PATTERN_BYTE( 0x21 ),             /* LD HL,nn */
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
  LOADER_PATTERN_BYTE( 0x06 ), LOADER_PATTERN_BYTE( 0x05 ), /* LD B,5 */
  LOADER_PATTERN_BYTE( 0xcd ),             /* CALL callback */
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
  LOADER_PATTERN_BYTE( 0xd0 ),             /* RET NC */
  LOADER_PATTERN_BYTE( 0xd9 ),             /* EXX */
};

static const loader_pattern_byte_t modified_rom_edge2_prefix[] = {
  LOADER_PATTERN_BYTE( 0xd9 ),             /* EXX */
  LOADER_PATTERN_BYTE( 0xa7 ),             /* AND A */
};

static int
modified_rom_loader_detector( libspectrum_word pc )
{
  /* Technician Ted replaces the ROM EDGE1 delay with useful work, entered
     with EXX and left immediately before the copied EDGE2 sampling loop. The
     work and the real sampling delay form one timing unit, so short-circuiting
     only EDGE2 corrupts the load. Keep the loop as strong autoplay evidence,
     but do not accelerate it. */
  return loader_pattern_matches( pc - 40, modified_rom_prefix,
                                 LOADER_PATTERN_LENGTH(
                                   modified_rom_prefix ) ) &&
         loader_word_matches( pc - 30, pc - 24 ) &&
         loader_pattern_matches( pc - 8, modified_rom_edge2_prefix,
                                 LOADER_PATTERN_LENGTH(
                                   modified_rom_edge2_prefix ) ) &&
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

static const loader_pattern_byte_t movieload_loader[] = {
  LOADER_PATTERN_BYTE( 0x14 ), LOADER_PATTERN_BYTE( 0xc8 ),
  LOADER_PATTERN_BYTE( 0x3e ), LOADER_PATTERN_BYTE( 0x7f ),
  ROM_LOADER_INPUT, LOADER_PATTERN_BYTE( 0x1f ),
  LOADER_PATTERN_BYTE( 0x00 ), LOADER_PATTERN_BYTE( 0xab ),
  LOADER_PATTERN_BYTE( 0xe6 ), LOADER_PATTERN_BYTE( 0x20 ),
  LOADER_PATTERN_BYTE( 0x28 ), LOADER_PATTERN_BYTE( 0xf3 ),
};

static int
movieload_loader_detector( libspectrum_word pc )
{
  /* INC D; RET Z; LD A,7f; IN A,(fe); RRA; NOP; XOR E;
     AND 20; JR Z,<INC D>. Movieload uses D rather than B as its counter. */
  return loader_pattern_matches( pc - 6, movieload_loader,
                                 LOADER_PATTERN_LENGTH( movieload_loader ) );
}

static const loader_pattern_byte_t sign_flag_loader[] = {
  LOADER_PATTERN_BYTE( 0x04 ), LOADER_PATTERN_BYTE( 0xc8 ),
  ROM_LOADER_INPUT, LOADER_PATTERN_BYTE( 0x87 ),
  /* JP P (f2) and JP M (fa) differ only in bit 3. */
  LOADER_PATTERN_MASKED( 0xf2, 0xf7 ),
  LOADER_PATTERN_ANY, LOADER_PATTERN_ANY,
};

static int
sign_flag_loader_detector( libspectrum_word pc )
{
  /* INC B; RET Z; IN A,(fe); ADD A,A; JP P/M,<INC B>. The loader tests
     EAR by moving bit 6 into the sign flag. */
  return loader_pattern_matches( pc - 4, sign_flag_loader,
                                 LOADER_PATTERN_LENGTH( sign_flag_loader ) ) &&
         loader_word_matches( pc + 2, pc - 4 );
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
  libspectrum_byte b, c, a_, a, f, l;
  libspectrum_word pc, sp;
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

typedef struct loader_detector_test_case_t {
  libspectrum_byte bytes[ 15 ];
  size_t length;
  size_t reject_offset;
  acceleration_mode_t mode;
} loader_detector_test_case_t;

static int
loader_test_acceleration_patterns( void )
{
  static const loader_detector_test_case_t tests[] = {
    { { 0x04, 0xc8, 0x3e, 0x00, 0xdb, 0xfe, 0x1f, 0xa9, 0xe6, 0x20,
        0x28, 0xf4 }, 12, 9, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f, 0x00, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0xff, 0xdb, 0xfe, 0x1f, 0xa7, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f, 0xc8, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f, 0xd0, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x00, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f, 0xa9,
        0xe6, 0x20, 0x28, 0xf2 }, 14, 5, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0xff, 0x3e, 0x7f, 0xdb, 0xfe, 0x1f, 0x00,
        0xa9, 0xe6, 0x20, 0x28, 0xf1 }, 15, 9,
      ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x00, 0xdb, 0xfe, 0xa9, 0xe6, 0x40, 0x28,
        0xf5 }, 11, 8, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0xc8, 0x3e, 0x7f, 0xdb, 0xfe, 0xa9, 0xe6, 0x40, 0xd8,
        0x00, 0x28, 0xf3 }, 13, 10, ACCELERATION_MODE_INCREASING },
    { { 0x03, 0xc3, 0x34, 0x12, 0xdb, 0xfe, 0x1f, 0xc8, 0xa9, 0xe6,
        0x20, 0x28, 0xf1 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x03, 0xc3, 0x78, 0x56, 0xdb, 0xfe, 0x1f, 0xc8, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 7, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0x20, 0x01, 0xc9, 0xdb, 0xfe, 0x1f, 0xc8, 0xa9, 0xe6,
        0x20, 0x28, 0xf1 }, 13, 2, ACCELERATION_MODE_INCREASING },
    { { 0x04, 0x20, 0x01, 0xc9, 0xdb, 0xfe, 0x1f, 0xc8, 0xa9, 0xe6,
        0x20, 0x28, 0xf3 }, 13, 2, ACCELERATION_MODE_INCREASING },
    { { 0x47, 0x08, 0x3e, 0x7f, 0xdb, 0xfe, 0xa9, 0xe6, 0x40, 0x20,
        0x04, 0x05, 0x20, 0xf4 }, 14, 10,
      ACCELERATION_MODE_SOFTWARE_PROJECTS },
  };
  int error = 0;

  for( size_t i = 0; i < sizeof( tests ) / sizeof( tests[ 0 ] ); i++ ) {
    loader_test_memory_t memory;
    loader_test_install( &memory, tests[ i ].bytes, tests[ i ].length );
    if( acceleration_detector( LOADER_TEST_BASE ) != tests[ i ].mode ) error++;
    writebyte_internal( LOADER_TEST_BASE + tests[ i ].reject_offset,
                        tests[ i ].bytes[ tests[ i ].reject_offset ] ^ 0x01 );
    if( acceleration_detector( LOADER_TEST_BASE ) != ACCELERATION_MODE_NONE )
      error++;
    loader_test_restore_memory( &memory );
  }

  return error;
}

static int
loader_test_digital_integration( void )
{
  static const libspectrum_byte loader[] = {
    0x00, 0x3f, 0x05, 0xc8, 0xdb, 0xfe, 0xa9, 0xe6, 0x40, 0xca,
    0xfc, 0x7f
  };
  loader_test_memory_t memory;
  libspectrum_word saved_pc = z80.pc.w;
  int error = 0;

  loader_test_install( &memory, loader, sizeof( loader ) );
  z80.pc.w = LOADER_TEST_BASE;
  if( acceleration_detector( LOADER_TEST_BASE ) !=
      ACCELERATION_MODE_DECREASING ) error++;

  /* The absolute branch must target the active sampling loop. */
  writebyte_internal( LOADER_TEST_BASE + 10, 0xfb );
  if( acceleration_detector( LOADER_TEST_BASE ) != ACCELERATION_MODE_NONE )
    error++;
  writebyte_internal( LOADER_TEST_BASE + 10, 0xfc );

  /* Opcodes which selected another original state-machine branch are invalid. */
  writebyte_internal( LOADER_TEST_BASE, 0x04 );
  if( acceleration_detector( LOADER_TEST_BASE ) != ACCELERATION_MODE_NONE )
    error++;

  z80.pc.w = saved_pc;
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
loader_test_rom_acceleration( void )
{
  loader_test_memory_t memory;
  int error = 0;

  loader_test_install( &memory, NULL, 2 );
  writebyte_internal( LOADER_TEST_BASE, 0x34 );
  writebyte_internal( LOADER_TEST_BASE + 1, 0x12 );
  z80.bc.b.l = 0xff;
  z80.af.b.l = 0x00;
  z80.sp.w = LOADER_TEST_BASE;
  acceleration_mode = ACCELERATION_MODE_INCREASING;
  accelerate_loader( 1 );
  if( z80.bc.b.h != 0xfe || z80.pc.w != 0x1234 ||
      z80.sp.w != LOADER_TEST_BASE + 2 ) error++;
  if( z80.bc.b.l != ( tape_microphone ? 0xdf : 0xff ) ||
      !( z80.af.b.l & 0x01 ) ) error++;

  z80.sp.w = LOADER_TEST_BASE;
  acceleration_mode = ACCELERATION_MODE_DECREASING;
  accelerate_loader( 1 );
  if( z80.bc.b.h != 0x00 || z80.pc.w != 0x1234 ||
      z80.sp.w != LOADER_TEST_BASE + 2 ) error++;

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
  acceleration_mode = ACCELERATION_MODE_SOFTWARE_PROJECTS;
  z80.af_.b.h = 0x24;
  accelerate_loader( 0 );
  if( z80.bc.b.h != 0x1a || z80.pc.w != LOADER_TEST_BASE + 15 ) error++;
  accelerate_loader( 1 );
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

  writebyte_internal( LOADER_TEST_BASE + 12, 0xff );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 16 ) !=
      ACCELERATION_MODE_NONE ) error++;
  writebyte_internal( LOADER_TEST_BASE + 12, loader[ 12 ] );

  /* Do not accept a falling-edge branch to a different loop. */
  writebyte_internal( LOADER_TEST_BASE + 18, 0x0c );
  if( acceleration_detector_at_in( LOADER_TEST_BASE + 16 ) !=
      ACCELERATION_MODE_NONE ) error++;
  writebyte_internal( LOADER_TEST_BASE + 18, loader[ 18 ] );

  z80.hl.b.l = 1;             /* First INC L has executed. */
  acceleration_mode = ACCELERATION_MODE_GREMLIN_RISING;
  acceleration_pc = LOADER_TEST_BASE + 5;
  accelerate_loader( 0 );
  if( z80.hl.b.l != 24 || z80.pc.w != LOADER_TEST_BASE + 9 ) error++;
  z80.hl.b.l++;                /* INC L in the falling-edge loop. */
  acceleration_mode = ACCELERATION_MODE_GREMLIN_FALLING;
  acceleration_pc = LOADER_TEST_BASE + 16;
  accelerate_loader( 0 );
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
  writebyte_internal( LOADER_TEST_BASE + 5, 0xf2 );
  if( !sign_flag_loader_detector( LOADER_TEST_BASE + 4 ) ) error++;
  writebyte_internal( LOADER_TEST_BASE + 5, loader[ 5 ] );
  writebyte_internal( LOADER_TEST_BASE + 4, 0x86 );
  if( sign_flag_loader_detector( LOADER_TEST_BASE + 4 ) ) error++;
  writebyte_internal( LOADER_TEST_BASE + 4, loader[ 4 ] );
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

  writebyte_internal( LOADER_TEST_BASE, 0xd8 );
  if( modified_rom_loader_detector( LOADER_TEST_BASE + 40 ) ) error++;
  writebyte_internal( LOADER_TEST_BASE, loader[ 0 ] );

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
  state.c = z80.bc.b.l;
  state.a_ = z80.af_.b.h;
  state.a = z80.af.b.h;
  state.f = z80.af.b.l;
  state.l = z80.hl.b.l;
  state.pc = z80.pc.w;
  state.sp = z80.sp.w;
  state.acceleration_pc = acceleration_pc;
  state.acceleration_mode = acceleration_mode;
  return state;
}

static void
loader_test_restore_state( const loader_test_state_t *state )
{
  z80.bc.b.h = state->b;
  z80.bc.b.l = state->c;
  z80.af_.b.h = state->a_;
  z80.af.b.h = state->a;
  z80.af.b.l = state->f;
  z80.hl.b.l = state->l;
  z80.pc.w = state->pc;
  z80.sp.w = state->sp;
  acceleration_pc = state->acceleration_pc;
  acceleration_mode = state->acceleration_mode;
}

int
loader_unittest( void )
{
  loader_test_state_t state = loader_test_save_state();
  int error = 0;

  error += loader_test_pattern_matcher();
  error += loader_test_acceleration_patterns();
  error += loader_test_digital_integration();
  error += loader_test_microprose();
  error += loader_test_rom_acceleration();
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
