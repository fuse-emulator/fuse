/* host-operations-capture.c: host operation integration tests: capture
   Copyright (c) 2026 Fredrick Meunier

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
*/

#include "host-operations.h"

static void
compare_screenshot_files( const char *host, const char *legacy )
{
  utils_file a, b;
  assert( !utils_read_file( host, &a ) && !utils_read_file( legacy, &b ) );
  assert( a.length && a.length == b.length && !memcmp( a.buffer, b.buffer,
                                                       a.length ) );
  utils_file_free( &a );
  utils_file_free( &b );
  assert( !remove( host ) && !remove( legacy ) );
}

#ifdef HAVE_LIB_XML2
static void
check_svg_document( const char *path, int drawing )
{
  utils_file file;
  assert( !utils_read_file( path, &file ) );
  assert( g_strstr_len( (const char *)file.buffer, file.length, "<svg" ) );
  assert( g_strstr_len( (const char *)file.buffer, file.length, "</svg>" ) );
  if( drawing )
    assert( g_strstr_len( (const char *)file.buffer, file.length, "<line " ) );
  utils_file_free( &file );
  assert( !remove( path ) );
}

#endif

void
check_profile( void )
{
  utils_file file;
  int active;
  unsigned char nop = 0;

  assert( fuse_host_copy_profile_state( &active ) == OK && !active );
  assert( fuse_host_finish_profile( "unused.csv" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  writebyte_internal( 0x8000, nop );
  writebyte_internal( 0x8001, nop );
  z80.pc.w = 0x8000;
  assert( fuse_host_start_profile() == OK );
  assert( fuse_host_start_profile() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_tstates( 5 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_finish_profile( "missing-directory/profile.csv" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_profile_state( &active ) == OK && active );
  assert( fuse_host_finish_profile( "profile.csv" ) == OK );
  assert( fuse_host_copy_profile_state( &active ) == OK && !active );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK );
  assert( !utils_read_file( "profile.csv", &file ) );
  assert( file.length == sizeof( "0x8000,4\n" ) - 1 &&
          !memcmp( file.buffer, "0x8000,4\n", file.length ) );
  utils_file_free( &file );
  assert( !remove( "profile.csv" ) );
}

void
check_psg_logging( void )
{
  static const unsigned char program[] = {
    0x01, 0xfd, 0xff, 0x3e, 8, 0xed, 0x79, /* Select AY register 8. */
    0x06, 0xbf, 0x3e, 15, 0xed, 0x79, 0x76  /* Write value 15; HALT. */
  };
  static const unsigned char expected[] = {
    'P', 'S', 'G', 0x1a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xff, 8, 15, 0xff, 0xff
  };
  utils_file file;
  int active;
  size_t i;

  assert( fuse_host_select_machine( "128" ) == OK );
  assert( fuse_host_copy_psg_logging( &active ) == OK && !active );
  assert( fuse_host_stop_psg_logging() == OK );
  assert( fuse_host_start_psg_logging( "" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_start_psg_logging( "missing-directory/out.psg" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_psg_logging( &active ) == OK && !active );
  for( i = 0; i < sizeof( program );
       i++ ) writebyte_internal( 0x8000 + i, program[i] );
  z80.pc.w = 0x8000;
  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_start_psg_logging( "ay.psg" ) == OK );
  assert( fuse_host_start_psg_logging( "unused.psg" ) ==
          FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_copy_psg_logging( &active ) == OK && active );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_psg_logging() == OK );
  assert( fuse_host_copy_psg_logging( &active ) == OK && !active );
  assert( !utils_read_file( "ay.psg", &file ) );
  assert( file.length == sizeof( expected ) && !memcmp( file.buffer, expected,
                                                        sizeof( expected ) ) );
  utils_file_free( &file );
  assert( !remove( "ay.psg" ) );
}

void
check_live_movie( void )
{
  fuse_movie_state state;
  utils_file file;

  assert( fuse_host_copy_movie_state( &state ) == OK &&
          state == FUSE_MOVIE_IDLE );
  assert( fuse_host_toggle_movie_pause() == OK );
  assert( fuse_host_stop_movie() == OK );
  assert( fuse_host_start_movie( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_start_movie( "missing-directory/movie" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_movie_state( &state ) == OK &&
          state == FUSE_MOVIE_IDLE );

  settings_set_string( &settings_current.movie_compr, "None" );
  assert( !utils_write_file( "live.fmf", (const unsigned char *)"old", 3 ) );
  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_start_movie( "live" ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_toggle_movie_pause() == OK );
  assert( fuse_host_copy_movie_state( &state ) == OK &&
          state == FUSE_MOVIE_PAUSED );
  assert( fuse_host_start_movie( "not-created" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_toggle_movie_pause() == OK );
  assert( fuse_host_copy_movie_state( &state ) == OK &&
          state == FUSE_MOVIE_RECORDING );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_movie() == OK );
  assert( fuse_host_copy_movie_state( &state ) == OK &&
          state == FUSE_MOVIE_IDLE );
  assert( !utils_read_file( "live.fmf", &file ) );
  assert( file.length > 16 && !memcmp( file.buffer, "FMF_V1", 6 ) );
  assert( file.buffer[7] == 'U' && file.buffer[file.length - 1] == 'X' );
  utils_file_free( &file );
  assert( !remove( "live.fmf" ) );
}

void
check_svg_capture( void )
{
  fuse_svg_capture_info info;
  assert( fuse_host_copy_svg_capture_info( &info ) == OK && !info.active );
  assert( fuse_host_start_svg_capture( "",
                                       SVG_CAPTURE_LINES ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_start_svg_capture( "lines",
                                       99 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
#ifdef HAVE_LIB_XML2
  assert( info.available );
  assert( fuse_host_stop_svg_capture() == OK );
  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_start_svg_capture( "lines", SVG_CAPTURE_LINES ) == OK );
  assert( fuse_host_start_svg_capture( "other",
                                       SVG_CAPTURE_DOTS ) ==
          FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_copy_svg_capture_info( &info ) == OK && info.active &&
          info.mode == SVG_CAPTURE_LINES );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK );
  z80.pc.w = 0x24ba; /* DRAW in the standard ROM. */
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
  z80.pc.w = 0x0daf; /* CLS after drawing produces a numbered segment. */
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
  check_svg_document( "lines0.svg", 1 );
  assert( fuse_host_stop_svg_capture() == OK );
  check_svg_document( "lines1.svg", 0 );

  assert( fuse_host_start_svg_capture( "dots", SVG_CAPTURE_DOTS ) == OK );
  assert( fuse_host_copy_svg_capture_info( &info ) == OK && info.active &&
          info.mode == SVG_CAPTURE_DOTS );
  z80.pc.w = 0x22e5; /* PLOT in dot mode. */
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_svg_capture() == OK );
  check_svg_document( "dots0.svg", 1 );

  assert( fuse_host_start_svg_capture( "missing-directory/capture",
                                       SVG_CAPTURE_LINES ) == OK );
  assert( fuse_host_stop_svg_capture() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_svg_capture_info( &info ) == OK && !info.active );
  assert( fuse_host_start_svg_capture( "recovered", SVG_CAPTURE_LINES ) == OK );
  fuse_shutdown();
  check_svg_document( "recovered0.svg", 0 );
#else
  assert( !info.available );
  assert( fuse_host_start_svg_capture( "lines",
                                       SVG_CAPTURE_LINES ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
#endif
}

void
check_screenshots( void )
{
  unsigned char scr[ 6912 ] = { 0x81 };
  unsigned char mlt[ 12288 ] = { 0x42 };
  unsigned char value;
  const char *id;
  assert( !utils_write_file( "input.scr", scr, sizeof( scr ) ) );
  assert( !utils_write_file( "input.mlt", mlt, sizeof( mlt ) ) );
  assert( fuse_host_set_paused( 1 ) == OK );
  assert( fuse_host_load_screenshot( FUSE_SCREENSHOT_SCR, "input.scr" ) == OK );
  assert( fuse_host_copy_memory( 0x4000, 1, &value ) == OK && value == scr[0] );
  assert( fuse_host_load_screenshot( FUSE_SCREENSHOT_MLT, "input.mlt" ) == OK );
  assert( fuse_host_copy_memory( 0x4000, 1, &value ) == OK && value == mlt[0] );
  assert( fuse_host_load_screenshot( FUSE_SCREENSHOT_SCR,
                                     "missing.scr" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_load_screenshot( FUSE_SCREENSHOT_PNG,
                                     "input.png" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_set_paused( 0 ) == OK &&
          fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_SCR, "host.scr",
                                     NULL ) == OK );
  assert( !screenshot_scr_write( "legacy.scr" ) );
  compare_screenshot_files( "host.scr", "legacy.scr" );
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_MLT, "host.mlt",
                                     NULL ) == OK );
  assert( !screenshot_mlt_write( "legacy.mlt" ) );
  compare_screenshot_files( "host.mlt", "legacy.mlt" );
  assert( fuse_host_png_scaler_id( 0, &id ) == OK );
#ifdef USE_LIBPNG
  assert( id && !strcmp( id, "normal" ) );
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, "host.png",
                                     "2x" ) == OK );
  assert( !screenshot_write( "legacy.png", SCALER_DOUBLESIZE ) );
  compare_screenshot_files( "host.png", "legacy.png" );
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, "invalid.png",
                                     "not-a-scaler" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
#else
  assert( !id );
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, "host.png",
                                     "normal" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
#endif
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_SCR,
                                     "missing-directory/screen.scr",
                                     NULL ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_select_machine( "2068" ) == OK );
  scld_last_dec.name.hires = 1;
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_MLT, "invalid.mlt",
                                     NULL ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
#ifdef USE_LIBPNG
  assert( fuse_host_save_screenshot( FUSE_SCREENSHOT_PNG, "invalid.png",
                                     "2x" ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
#endif
  assert( !remove( "input.scr" ) && !remove( "input.mlt" ) );
}
