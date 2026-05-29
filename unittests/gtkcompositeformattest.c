/* gtkcompositeformattest.c: Regression tests for 32-bit composite scaler
   pixel-format conversions, including GTK/Cairo-style x8r8g8b8 packing
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libspectrum.h"

#include "settings.h"
#include "ui/scaler/scaler.h"
#include "ui/scaler/scaler_internals.h"
#include "ui/ui.h"

#define TEST_WIDTH 6
#define TEST_ROW_PIXELS ( TEST_WIDTH + 3 )
#define TEST_SCALE 2
#define TEST_OUTPUT_PIXELS ( TEST_WIDTH * TEST_SCALE * TEST_SCALE )

settings_info settings_current;

char*
utils_safe_strdup( const char *s )
{
  char *copy;
  size_t length;

  if( !s ) return NULL;

  length = strlen( s ) + 1;
  copy = libspectrum_malloc( length );
  memcpy( copy, s, length );

  return copy;
}

int
uidisplay_hotswap_gfx_mode( void )
{
  return 0;
}

int
ui_error( ui_error_level severity GCC_UNUSED, const char *format GCC_UNUSED, ... )
{
  return 0;
}

static libspectrum_dword
pack_rgb( libspectrum_byte red, libspectrum_byte green, libspectrum_byte blue )
{
  return red | green << 8 | blue << 16;
}

static libspectrum_dword
pack_x8r8g8b8( libspectrum_byte red, libspectrum_byte green,
               libspectrum_byte blue )
{
#ifdef WORDS_BIGENDIAN
  return green << 8 | red << 16 | blue;
#else
  return blue | green << 8 | red << 16;
#endif
}

static libspectrum_dword
x8r8g8b8_to_rgb( libspectrum_dword pixel )
{
#ifdef WORDS_BIGENDIAN
  libspectrum_byte red = ( pixel >> 16 ) & 0xff;
  libspectrum_byte green = ( pixel >> 8 ) & 0xff;
  libspectrum_byte blue = pixel & 0xff;
#else
  libspectrum_byte blue = pixel & 0xff;
  libspectrum_byte green = ( pixel >> 8 ) & 0xff;
  libspectrum_byte red = ( pixel >> 16 ) & 0xff;
#endif

  return pack_rgb( red, green, blue );
}

static void
build_rows( libspectrum_dword *rgb, libspectrum_dword *x8r8g8b8 )
{
  static const libspectrum_dword pixels[ TEST_WIDTH ][ 3 ] = {
    { 0xff, 0x00, 0x00 },
    { 0x00, 0xff, 0x00 },
    { 0x00, 0x00, 0xff },
    { 0xff, 0xff, 0x00 },
    { 0xff, 0x00, 0xff },
    { 0x00, 0xff, 0xff }
  };
  int i;

  for( i = 0; i < TEST_WIDTH; i++ ) {
    rgb[i + 1] = pack_rgb( pixels[i][0], pixels[i][1], pixels[i][2] );
    x8r8g8b8[i + 1] = pack_x8r8g8b8( pixels[i][0], pixels[i][1], pixels[i][2] );
  }

  rgb[0] = rgb[1];
  x8r8g8b8[0] = x8r8g8b8[1];
  rgb[TEST_WIDTH + 1] = rgb[TEST_WIDTH];
  rgb[TEST_WIDTH + 2] = rgb[TEST_WIDTH];
  x8r8g8b8[TEST_WIDTH + 1] = x8r8g8b8[TEST_WIDTH];
  x8r8g8b8[TEST_WIDTH + 2] = x8r8g8b8[TEST_WIDTH];
}

/* This verifies the scaler-core pixel-format contract for 32-bit
   composite output. It does not exercise the GTK frontend directly; it
   checks that equivalent source rows in the native scaler format and in
   GTK/Cairo-style x8r8g8b8 format produce equivalent composite output
   once the latter is converted back to native RGB ordering. */
static int
compare_outputs( const libspectrum_dword *rgb_output,
                 const libspectrum_dword *x8r8g8b8_output )
{
  int i;

  for( i = 0; i < TEST_OUTPUT_PIXELS; i++ ) {
    libspectrum_dword converted = x8r8g8b8_to_rgb( x8r8g8b8_output[i] );

    if( rgb_output[i] != converted ) {
      fprintf( stderr,
               "output mismatch at %d: rgb=0x%06x x8r8g8b8=0x%06x converted=0x%06x\n",
               i, (unsigned)rgb_output[i], (unsigned)x8r8g8b8_output[i],
               (unsigned)converted );
      return 1;
    }
  }

  return 0;
}

#ifdef main
/* SDL headers redefine main on Windows, but this test needs a normal entry point. */
#undef main
#endif

int
main( void )
{
  libspectrum_dword rgb_src[ TEST_ROW_PIXELS ];
  libspectrum_dword x8r8g8b8_src[ TEST_ROW_PIXELS ];
  libspectrum_dword rgb_output[ TEST_OUTPUT_PIXELS ];
  libspectrum_dword x8r8g8b8_output[ TEST_OUTPUT_PIXELS ];

  memset( &settings_current, 0, sizeof( settings_current ) );
  memset( rgb_output, 0, sizeof( rgb_output ) );
  memset( x8r8g8b8_output, 0, sizeof( x8r8g8b8_output ) );
  build_rows( rgb_src, x8r8g8b8_src );

  if( scaler_select_bitformat( BITFORMAT_X8B8G8R8 ) ) return 1;
  scaler_PalTV2x_32( (const libspectrum_byte*)&rgb_src[1],
                     TEST_ROW_PIXELS * sizeof( rgb_src[0] ),
                     (libspectrum_byte*)rgb_output,
                     TEST_WIDTH * TEST_SCALE * sizeof( rgb_output[0] ),
                     TEST_WIDTH, 1 );

  if( scaler_select_bitformat( BITFORMAT_X8R8G8B8 ) ) return 1;
  scaler_PalTV2x_32( (const libspectrum_byte*)&x8r8g8b8_src[1],
                     TEST_ROW_PIXELS * sizeof( x8r8g8b8_src[0] ),
                     (libspectrum_byte*)x8r8g8b8_output,
                     TEST_WIDTH * TEST_SCALE * sizeof( x8r8g8b8_output[0] ),
                     TEST_WIDTH, 1 );

  return compare_outputs( rgb_output, x8r8g8b8_output );
}
