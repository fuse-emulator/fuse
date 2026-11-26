/* floatingbus.c: Optional original-program visual qualification
   Copyright (c) 2026 Fuse contributors

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

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir( path )
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir( (path), 0700 )
#define PATH_SEP '/'
#endif

#include "fuse-host-media.h"
#include "fuse-host-settings.h"
#include "display.h"
#include "event.h"
#include "machine.h"
#include "memory_pages.h"
#include "machines/specplus3.h"
#include "peripherals/ula.h"
#include "screenshot.h"
#include "settings.h"
#include "snapshot.h"
#include "spectrum.h"
#include "tape.h"
#include "ui/ui.h"
#include "unittests.h"
#include "z80/z80.h"
#include "z80/z80_macros.h"

/* Pre-optimization arithmetic reference. This deliberately has no next-fetch
   cache and replays no new hardware assumptions: it checks observational
   equivalence for real transfers, not physical timing certification or
   bus history spanning out-of-machine RAM edits. */
typedef struct bus_reference {
  long cursor;
  int page;
  libspectrum_byte value;
} bus_reference;

static void
reference_sync( bus_reference *bus, long time )
{
  long frame = machine_current->timings.tstates_per_frame;
  long first = machine_current->line_times[DISPLAY_BORDER_HEIGHT] +
               DISPLAY_BORDER_WIDTH_COLS * 4 + 2;
  long local = time % frame;
  long fetch = time - local;
  long line, slot;
  int column, offset;

  if( local < first ) {
    fetch -= frame;
    line = DISPLAY_HEIGHT - 1;
    slot = 123;
  } else {
    line = ( local - first ) / machine_current->timings.tstates_per_line;
    slot = ( local - first ) % machine_current->timings.tstates_per_line;
    if( line >= DISPLAY_HEIGHT ) {
      line = DISPLAY_HEIGHT - 1;
      slot = 123;
    } else if( slot > 123 ) {
      slot = 123;
    } else if( ( slot & 7 ) > 3 ) {
      slot = ( slot & ~7 ) + 3;
    }
  }
  fetch += first + line * machine_current->timings.tstates_per_line + slot;
  if( fetch > bus->cursor ) {
    column = 2 * ( slot / 8 ) + ( slot & 3 ) / 2;
    offset = ( slot & 1 ) ? display_attr_start[line] : display_line_start[line];
    bus->value = RAM[bus->page][offset + column];
  }
  bus->cursor = time;
}

static libspectrum_dword
reference_random( libspectrum_dword *seed )
{
  *seed ^= *seed << 13;
  *seed ^= *seed >> 17;
  *seed ^= *seed << 5;
  return *seed;
}

int
floatingbus_differential_unittest( void )
{
  static const libspectrum_machine models[] = {
    LIBSPECTRUM_MACHINE_PLUS2A, LIBSPECTRUM_MACHINE_PLUS3,
    LIBSPECTRUM_MACHINE_PLUS3E
  };
  libspectrum_machine saved = machine_current->machine;
  libspectrum_dword seed = 0x3c2a7654, random, sample, length;
  libspectrum_word address;
  libspectrum_byte value, expected;
  bus_reference bus;
  memory_page *mapping;
  int m, late, saved_late = settings_current.late_timings;
  int step, page, offset, error = 0;

  for( m = 0; m < 3; m++ ) {
    for( late = 0; late < 2; late++ ) {
      settings_current.late_timings = late;
      if( machine_select( models[m] ) ) { error++; continue; }
      length = machine_current->timings.tstates_per_frame;
      for( page = 5; page <= 7; page += 2 )
        for( offset = 0; offset < 0x4000; offset++ )
          RAM[page][offset] = reference_random( &seed );
      specplus3_bus_rebase();
      bus.cursor = tstates; bus.page = memory_current_screen; bus.value = 0xff;
      for( step = 0; step < 10000; step++ ) {
        random = reference_random( &seed );
        tstates += ( step & 1 ) ? ( random & 2047 ) : ( random & 15 );
        if( tstates >= length ) {
          reference_sync( &bus, tstates );
          specplus3_bus_frame( length, tstates );
          bus.cursor -= length;
          tstates -= length;
        }
        value = random >> 24;
        address = 0x4000 + ( ( random >> 8 ) & 0x3fff );
        mapping = &memory_map_read[address >> MEMORY_PAGE_SIZE_LOGARITHM];
        switch( ( random >> 16 ) & 7 ) {
        case 0: /* Timed read, physical slow RAM. */
          sample = tstates + ula_contention[tstates] + 3;
          reference_sync( &bus, sample );
          bus.value = mapping->page[address & MEMORY_PAGE_SIZE_MASK];
          readbyte( address );
          break;
        case 1: /* Timed write, including unchanged storage values. */
          sample = tstates + ula_contention[tstates] + 3;
          reference_sync( &bus, sample );
          bus.value = value;
          writebyte( address, value );
          break;
        case 2: /* An unchanged-value CPU write still drives the bus. */
          value = mapping->page[address & MEMORY_PAGE_SIZE_MASK];
          sample = tstates + ula_contention[tstates] + 3;
          reference_sync( &bus, sample );
          bus.value = value;
          writebyte( address, value );
          break;
        case 3: /* An M1 sample precedes its refresh T-state. */
          sample = tstates + ula_contention[tstates] + 3;
          reference_sync( &bus, sample );
          bus.value = mapping->page[address & MEMORY_PAGE_SIZE_MASK];
          contend_read( address, 4 );
          break;
        case 4: /* Unconsumed same-slot source changes use the new page. */
          page = ( random & 0x80 ) ? 7 : 5;
          if( page != bus.page ) {
            if( (long)tstates > bus.cursor ) reference_sync( &bus, (long)tstates - 1 );
            bus.page = page;
          }
          writeport_internal( 0x7ffd, page == 7 ? 8 : 0 );
          break;
        case 5: /* A legitimate rebase must not replay prior fetches. */
          specplus3_bus_invalidate();
          specplus3_bus_rebase();
          bus.cursor = tstates; bus.value = 0xff;
          break;
        case 6: /* A non-driving fast-RAM transfer still allows video time. */
          readbyte( 0xa000 );
          break;
        default: /* Pure elapsed time / repeated query. */
          break;
        }
        reference_sync( &bus, tstates );
        expected = bus.value | 1;
        if( specplus3_unattached_port( 1 ) != expected ||
            specplus3_unattached_port( 1 ) != expected ) {
          printf( "Bus differential failed model=%d late=%d step=%d time=%u\n",
                  models[m], late, step, tstates );
          error++;
          break;
        }
      }
    }
  }
  settings_current.late_timings = saved_late;
  if( machine_select( saved ) ) error++;
  return error;
}

/* Fixture workflows run from the READY host integration runner. Opcode-level
   timing observations remain internal; machine/media/capture use the API. */
static int
fixture_select_machine( libspectrum_machine model )
{
  return fuse_host_select_machine( machine_get_id( model ) ) != FUSE_HOST_OPERATION_OK;
}

/* Copy the current record before each apply: machine selection can change
   other settings, which must not be rolled back by these fixture options. */
static int
fixture_apply_options( const settings_info *options )
{
  settings_info *candidate;
  fuse_host_operation_result result;

  if( fuse_host_copy_settings( &candidate ) != FUSE_HOST_OPERATION_OK ) return 1;
  candidate->tape_traps = options->tape_traps;
  candidate->accelerate_loader = options->accelerate_loader;
  candidate->emulation_speed = options->emulation_speed;
  candidate->late_timings = options->late_timings;
  candidate->frame_rate = options->frame_rate;
  result = fuse_host_apply_settings( candidate, 0 );
  fuse_host_free_settings( candidate );
  return result != FUSE_HOST_OPERATION_OK;
}

static int
fixture_open_tape( const char *path )
{
  fuse_open_request request;
  return fuse_host_open_file( path, &request ) != FUSE_HOST_OPERATION_OK || request.id;
}

static int
fixture_eject_tape( void )
{
  fuse_media_decision_id request;
  fuse_host_operation_result result = fuse_host_request_tape_eject( &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION )
    result = fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL );
  return result != FUSE_HOST_OPERATION_OK;
}

/* The benchmark programs never read I/O, so their CPU/memory behavior is
   independent of floating-bus values. Identical SZX files feed both binaries.
   Slow code/data are outside the display file to avoid dirty-screen work. */
int
floatingbus_performance_fixtures( void )
{
  const char *directory = getenv( "FUSE_TEST_FLOATING_PERF_OUTPUT" );
  static const libspectrum_machine models[] = {
    LIBSPECTRUM_MACHINE_48, LIBSPECTRUM_MACHINE_128,
    LIBSPECTRUM_MACHINE_PLUS3
  };
  libspectrum_machine saved = machine_current->machine;
  libspectrum_word code, data;
  int m, slow, n, error = 0;
  char filename[1024];

  if( !directory ) return 0;
  for( m = 0; m < 3; m++ ) {
    for( slow = 0; slow < 2; slow++ ) {
      if( fixture_select_machine( models[m] ) ) { error++; continue; }
      code = slow ? 0x6100 : 0x9000;
      data = slow ? 0x7a00 : 0xa000;
      /* LD HL,data; loop: INC (HL); JR loop. */
      writebyte_internal( code, 0x21 );
      writebyte_internal( code + 1, data & 0xff );
      writebyte_internal( code + 2, data >> 8 );
      writebyte_internal( code + 3, 0x34 );
      writebyte_internal( code + 4, 0x18 );
      writebyte_internal( code + 5, 0xfd );
      writebyte_internal( data, 0 );
      PC = code; SP = 0xb000; IFF1 = IFF2 = 0; IM = 1;
      z80.halted = 0;
      n = snprintf( filename, sizeof(filename), "%s/%s-%s.szx", directory,
                    machine_current->id, slow ? "slow" : "fast" );
      if( n < 0 || n >= sizeof(filename) ||
          fuse_host_save_snapshot_file( filename ) != FUSE_HOST_OPERATION_OK ) error++;
    }
  }
  if( fixture_select_machine( saved ) ) error++;
  return error;
}

#ifdef USE_LIBPNG
#define VISUAL_FRAMES 256

static unsigned long program_samples;
static libspectrum_word program_end;

static int
run_frames( unsigned int count )
{
  libspectrum_dword first = spectrum_get_frame_count();
  unsigned long steps;

  for( steps = 0; steps < 20000000 &&
       spectrum_get_frame_count() - first < count; steps++ ) {
    z80_do_opcodes();
    if( PC >= 0x9c40 && PC < program_end ) program_samples++;
    if( event_do_events() ) return 1;
  }
  return spectrum_get_frame_count() - first != count;
}

/* Full logical display, including borders. These tests run ordinary Spectrum
   modes, not Timex hires. Ignore brightness but honor the rendered FLASH bit. */
static void
display_colours( unsigned int rows[ DISPLAY_SCREEN_HEIGHT ],
                 unsigned int upper[8],
                 unsigned int blue_border[ DISPLAY_SCREEN_HEIGHT ] )
{
  int x, y, bit, ink, paper, colour;
  libspectrum_dword chunk;

  memset( upper, 0, 8 * sizeof(*upper) );
  for( y = 0; y < DISPLAY_SCREEN_HEIGHT; y++ ) {
    rows[y] = blue_border[y] = 0;
    for( x = 0; x < DISPLAY_SCREEN_WIDTH_COLS; x++ ) {
      chunk = display_last_screen[y * DISPLAY_SCREEN_WIDTH_COLS + x];
      ink = ( chunk >> 8 ) & 7;
      paper = ( chunk >> 11 ) & 7;
      for( bit = 0; bit < 8; bit++ ) {
        colour = ( chunk >> bit ) & 1;
        if( ( chunk & 0x8000 ) && ( chunk & 0x01000000 ) ) colour ^= 1;
        colour = colour ? ink : paper;
        if( colour == 6 ) rows[y]++;
        if( colour == 1 &&
            ( y < DISPLAY_BORDER_HEIGHT ||
              y >= DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT ||
              x < DISPLAY_BORDER_WIDTH_COLS ||
              x >= DISPLAY_BORDER_WIDTH_COLS + DISPLAY_WIDTH_COLS ) )
          blue_border[y]++;
        if( y >= DISPLAY_BORDER_HEIGHT &&
            y < DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT * 2 / 3 &&
            x >= DISPLAY_BORDER_WIDTH_COLS &&
            x < DISPLAY_BORDER_WIDTH_COLS + DISPLAY_WIDTH_COLS )
          upper[colour]++;
      }
    }
  }
}

#endif /* USE_LIBPNG */

/* Create directory and all parent components. Returns 0 on success. */
static int
mkdir_p( const char *path )
{
  char tmp[1024], *p;
  size_t len = strlen( path );

  if( len >= sizeof(tmp) ) return -1;
  memcpy( tmp, path, len + 1 );
  /* Strip trailing separator */
  if( len > 1 && ( tmp[len-1] == '/' || tmp[len-1] == '\\' ) ) tmp[len-1] = '\0';
  for( p = tmp + 1; *p; p++ ) {
    if( *p == '/' || *p == '\\' ) {
      char sep = *p;
      *p = '\0';
      MKDIR( tmp );
      *p = sep;
    }
  }
  MKDIR( tmp );
  return 0;
}

/* Return 1 for a verified local fixture, 0 for an optional missing fixture,
   and -1 for a malformed path, unreadable file or checksum mismatch. */
static int
find_fixture( const char *name, const char *crc32_hex, char *path, size_t size )
{
  const char *srcdir = getenv( "FUSE_TEST_SRCDIR" );
  char *contents;
  long file_length;
  size_t length;
  FILE *f;
  int n, error;

  if( !srcdir ) srcdir = ".";
  n = snprintf( path, size, "%s/unittests/fixtures/floatingbus/%s", srcdir, name );
  if( n < 0 || n >= size ) {
    printf( "Floating-bus fixture path too long: %s\n", name );
    return -1;
  }

  f = fopen( path, "rb" );
  if( !f ) {
    if( errno == ENOENT )
      printf( "SKIP: optional floating-bus fixture missing: %s\n", path );
    else
      printf( "Floating-bus fixture read failed: %s: %s\n", path,
              strerror( errno ) );
    return errno == ENOENT ? 0 : -1;
  }

  if( fseek( f, 0, SEEK_END ) ) { fclose( f ); return -1; }
  file_length = ftell( f );
  if( file_length <= 0 ) { fclose( f ); return -1; }
  if( fseek( f, 0, SEEK_SET ) ) { fclose( f ); return -1; }

  contents = malloc( (size_t)file_length );
  if( !contents ) { fclose( f ); return -1; }
  length = fread( contents, 1, (size_t)file_length, f );
  fclose( f );
  if( length != (size_t)file_length ) { free( contents ); return -1; }

  error = 0;
#ifdef HAVE_ZLIB_H
  {
    uLong crc = crc32( 0, Z_NULL, 0 );
    char computed[9];
    crc = crc32( crc, (const Bytef *)contents, length );
    snprintf( computed, sizeof(computed), "%08lx", (unsigned long)crc );
    error = strcmp( computed, crc32_hex );
  }
#endif
  free( contents );
  if( error ) {
    printf( "Floating-bus fixture checksum mismatch: %s\n", path );
    return -1;
  }
  return 1;
}

static const char *
output_directory( void )
{
  const char *directory = getenv( "FUSE_TEST_FLOATING_VISUAL_OUTPUT" );

  if( !directory ) directory = "unittests/floatingbus-output";
  if( mkdir_p( directory ) ) {
    printf( "Cannot create floating-bus test output directory %s: %s\n",
            directory, strerror( errno ) );
    return NULL;
  }
  return directory;
}

/* Optional pinned Hikaru v1.2 qualification from the fixture subdirectory.
   Normal CPU/events and genuine held keys are used, not RZX IN data. */
int
floatingbus_original_unittest( void )
{
  static const libspectrum_machine models[] = {
    LIBSPECTRUM_MACHINE_PLUS2A, LIBSPECTRUM_MACHINE_PLUS3,
    LIBSPECTRUM_MACHINE_PLUS3E
  };
  libspectrum_machine saved = machine_current->machine;
  libspectrum_dword frame;
  unsigned long steps;
  const char *directory;
  int found, n, m, stage, column, scan, r = 0;
  static const char photographed_row[] =
    "14368:  83 03 85 05 05 05 05 05 ";
  settings_info *saved_options, options;
  char path[1024], filename[1024];

  found = find_fixture( "hikaru.tap", "b73dd642", path, sizeof(path) );
  if( found <= 0 ) return found < 0;
  directory = output_directory();
  if( !directory ) return 1;
  if( fuse_host_copy_settings( &saved_options ) != FUSE_HOST_OPERATION_OK ) return 1;
  options = *saved_options;
  options.emulation_speed = 100000;
  options.late_timings = 0;
  for( m = 0; m < 3; m++ ) {
    options.tape_traps = saved_options->tape_traps;
    options.accelerate_loader = saved_options->accelerate_loader;
    if( fixture_apply_options( &options ) ||
        fixture_select_machine( models[m] ) || fixture_open_tape( path ) ) { r++; break; }
    r += fuse_host_release_all() != FUSE_HOST_OPERATION_OK;
    for( stage = 0; stage < 5; stage++ ) {
      if( stage == 1 ) {
        if( readbyte_internal( 0x8000 ) != 0xf3 ||
            readbyte_internal( 0x8001 ) != 0x01 ) r++;
        options.tape_traps = options.accelerate_loader = 0;
        r += fixture_apply_options( &options );
        r += fuse_host_spectrum_key_event( FUSE_HOST_KEY_SPACE, 1 ) != FUSE_HOST_OPERATION_OK;
      }
      if( stage == 2 )
        r += fuse_host_spectrum_key_event( FUSE_HOST_KEY_SPACE, 0 ) != FUSE_HOST_OPERATION_OK;
      if( stage == 3 )
        r += fuse_host_spectrum_key_event( FUSE_HOST_KEY_M, 1 ) != FUSE_HOST_OPERATION_OK;
      if( stage == 4 )
        r += fuse_host_spectrum_key_event( FUSE_HOST_KEY_M, 0 ) != FUSE_HOST_OPERATION_OK;
      frame = spectrum_get_frame_count();
      for( steps = 0; steps < 20000000 &&
           spectrum_get_frame_count() - frame <
             ( stage == 0 ? 2400 : ( stage == 1 || stage == 3 ) ? 10 : 3000 ); steps++ ) {
        z80_do_opcodes();
        event_do_events();
      }
      if( steps == 20000000 ) r++;
      if( stage == 2 ) {
        /* Photographed numeric row, including its printed timestamp; this
           timestamp is not the internal video-fetch timestamp. */
        for( column = 0; column < sizeof(photographed_row) - 1; column++ )
          for( scan = 0; scan < 8; scan++ )
            if( RAM[5][display_line_start[16 + scan] + column] !=
                readbyte_internal( 0x3c00 + photographed_row[column] * 8 + scan ) )
              r++;
        /* Original sampler delay, not ASIC state. */
        printf( "Hikaru calibrated delay=%02x%02x\n",
                readbyte_internal( 0xc295 ), readbyte_internal( 0xc294 ) );
      }
      n = snprintf( filename, sizeof(filename), "%s/hikaru-%s-%d.scr",
                    directory, machine_current->id, stage );
      if( n < 0 || n >= sizeof(filename) ||
          fuse_host_save_screenshot( FUSE_SCREENSHOT_SCR, filename, NULL ) !=
            FUSE_HOST_OPERATION_OK ) r++;
      printf( "Hikaru model=%d stage=%d PC=%04x IFF=%d/%d IM=%d "
              "SP=%04x frames=%u screen=%s\n",
              models[m], stage, PC, IFF1, IFF2, IM, SP,
              spectrum_get_frame_count() - frame, filename );
    }
    r += fuse_host_release_all() != FUSE_HOST_OPERATION_OK;
    r += fixture_eject_tape();
  }
  r += fixture_apply_options( saved_options );
  fuse_host_free_settings( saved_options );
  if( fixture_select_machine( saved ) ) r++;
  return r;
}

#ifdef USE_LIBPNG
static int
run_visual( const char *path, const char *directory, int woody,
            libspectrum_machine machine, settings_info *options )
{
  unsigned int rows[ DISPLAY_SCREEN_HEIGHT ], upper[8];
  unsigned int blue_border[ DISPLAY_SCREEN_HEIGHT ], blue_ground, blue_escape;
  unsigned int top, ground, bottom, above, below;
  unsigned int with_top = 0, with_bottom = 0, without_extra = 0;
  int frame, y, error = 0, n;
  char filename[1024];
  FILE *csv;

  program_end = woody ? 0x9d61 : 0x9c66;
  if( fixture_select_machine( machine ) || fixture_open_tape( path ) ) return 1;
  if( fuse_host_release_all() != FUSE_HOST_OPERATION_OK ) return 1;
  if( run_frames( 2400 ) ) return 1;
  options->tape_traps = options->accelerate_loader = 0;
  if( fixture_apply_options( options ) ) return 1;

  n = snprintf( filename, sizeof(filename), "%s/%s-%s-before.png", directory,
                woody ? "woody" : "universal", machine_current->id );
  if( n < 0 || n >= sizeof(filename) ) return 1;
  if( fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, filename, "normal" ) !=
      FUSE_HOST_OPERATION_OK ) return 1;
  if( woody ) {
    display_colours( rows, upper, blue_border );
    /* The reference's striped upper field is a post-keypress effect. */
    if( upper[7] ) return 1;
    if( fuse_host_spectrum_key_event( FUSE_HOST_KEY_SPACE, 1 ) !=
        FUSE_HOST_OPERATION_OK ) return 1;
    error = run_frames( 10 );
    error += fuse_host_spectrum_key_event( FUSE_HOST_KEY_SPACE, 0 ) !=
             FUSE_HOST_OPERATION_OK;
    if( error ) return error;
  }
  if( run_frames( 50 ) ) return 1;

  n = snprintf( filename, sizeof(filename), "%s/%s-%s.szx", directory,
                woody ? "woody" : "universal", machine_current->id );
  if( n < 0 || n >= sizeof(filename) ||
      fuse_host_save_snapshot_file( filename ) != FUSE_HOST_OPERATION_OK ) return 1;
  n = snprintf( filename, sizeof(filename), "%s/%s-%s.csv", directory,
                woody ? "woody" : "universal", machine_current->id );
  if( n < 0 || n >= sizeof(filename) ) return 1;
  csv = fopen( filename, "w" );
  if( !csv ) return 1;
  fprintf( csv, "frame,top_border,ground,bottom_border,above_ground,below_ground,"
                "upper_blue,upper_white,program_samples,blue_ground,blue_escape" );
  program_samples = 0;
  for( y = 0; y < DISPLAY_SCREEN_HEIGHT; y++ ) fprintf( csv, ",y%d", y );
  fprintf( csv, "\n" );

  for( frame = 0; frame < VISUAL_FRAMES; frame++ ) {
    if( run_frames( 1 ) ) { error++; break; }
    display_colours( rows, upper, blue_border );
    if( woody && ( upper[1] < 1000 || upper[7] < 1000 ) ) error++;
    top = ground = bottom = above = below = 0;
    blue_ground = blue_escape = 0;
    for( y = 0; y < DISPLAY_SCREEN_HEIGHT; y++ ) {
      if( y >= DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT * 2 / 3 &&
          y < DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT )
        blue_ground += blue_border[y];
      else
        blue_escape += blue_border[y];
      if( y < DISPLAY_BORDER_HEIGHT ) top += rows[y];
      if( y >= DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT ) bottom += rows[y];
      /* Conservative reference geometry: ground lies in the lower third
         of the active display, not the upper field or vertical borders.
         This is not an assertion of a measured T-state boundary. */
      if( y < DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT * 2 / 3 )
        above += rows[y];
      else if( y < DISPLAY_BORDER_HEIGHT + DISPLAY_HEIGHT )
        ground += rows[y];
      else
        below += rows[y];
    }
    if( !ground ) error++;
    /* Universal OUTs blue, then white, in response to the sampled 71h.
       Its static yellow attribute marker alone is not a positive control:
       an FF-only implementation would leave that marker visible too. */
    if( !woody && ( above || below || !blue_ground || blue_escape ) ) error++;
    if( top ) with_top++;
    if( bottom ) with_bottom++;
    if( !top && !bottom ) without_extra++;
    fprintf( csv, "%d,%u,%u,%u,%u,%u,%u,%u,%lu,%u,%u", frame, top,
             ground, bottom, above, below, upper[1], upper[7], program_samples,
             blue_ground, blue_escape );
    for( y = 0; y < DISPLAY_SCREEN_HEIGHT; y++ ) fprintf( csv, ",%u", rows[y] );
    fprintf( csv, "\n" );
    n = snprintf( filename, sizeof(filename), "%s/%s-%s-%03d.png", directory,
                  woody ? "woody" : "universal", machine_current->id, frame );
    if( n < 0 || n >= sizeof(filename) ||
        fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, filename, "normal" ) !=
          FUSE_HOST_OPERATION_OK ) { error++; break; }
  }
  if( ferror( csv ) ) error++;
  if( fclose( csv ) ) error++;
  if( !program_samples ) error++;
  if( woody && ( !with_top || !with_bottom || !without_extra ) ) error++;
  printf( "Floating-bus visual %s %s: frames=%d top=%u bottom=%u "
          "neither=%u errors=%d\n", woody ? "woody" : "universal",
          machine_current->id, frame, with_top, with_bottom, without_extra, error );
  return error;
}

int
floatingbus_visual_unittest( void )
{
  static const libspectrum_machine models[] = {
    LIBSPECTRUM_MACHINE_PLUS2A, LIBSPECTRUM_MACHINE_PLUS3,
    LIBSPECTRUM_MACHINE_PLUS3E
  };
  static const char * const hashes[] = {
    "9a33dc99", "5b8793bb"
  };
  static const char * const names[] = { "universal.tap", "woody.tap" };
  char paths[2][1024];
  const char *directory;
  int found[2];
  libspectrum_machine saved = machine_current->machine;
  settings_info *saved_options, options;
  int which, m, error = 0;

  for( which = 0; which < 2; which++ ) {
    found[which] = find_fixture( names[which], hashes[which], paths[which],
                                 sizeof(paths[which]) );
    if( found[which] < 0 ) error++;
  }
  if( found[0] <= 0 && found[1] <= 0 ) return error;
  directory = output_directory();
  if( !directory ) return error + 1;
  if( fuse_host_copy_settings( &saved_options ) != FUSE_HOST_OPERATION_OK ) return error + 1;
  options = *saved_options;
  options.emulation_speed = 100000;
  options.late_timings = 0;
  options.frame_rate = 1;
  for( which = 0; which < 2; which++ ) {
    if( found[which] <= 0 ) continue;
    for( m = 0; m < 3; m++ ) {
      options.tape_traps = saved_options->tape_traps;
      options.accelerate_loader = saved_options->accelerate_loader;
      if( fixture_apply_options( &options ) ) { error++; break; }
      error += run_visual( paths[which], directory, which, models[m], &options );
      error += fuse_host_release_all() != FUSE_HOST_OPERATION_OK;
      error += fixture_eject_tape();
    }
  }
  error += fixture_apply_options( saved_options );
  fuse_host_free_settings( saved_options );
  if( fixture_select_machine( saved ) ) error++;
  return error;
}
#else
int
floatingbus_visual_unittest( void )
{
  puts( "SKIP: floating-bus visual tests require PNG support" );
  return 0;
}
#endif /* USE_LIBPNG */
