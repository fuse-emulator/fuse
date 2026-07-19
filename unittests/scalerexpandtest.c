/* scalerexpandtest.c: regression tests for scaler dirty-region expansion
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "config.h"

#include <stdio.h>

#include "settings.h"
#include "ui/scaler/scaler.h"
#include "ui/ui.h"
#include "utils.h"

settings_info settings_current;

int
ui_error( ui_error_level severity GCC_UNUSED, const char *format GCC_UNUSED, ... )
{
  return 0;
}

int
uidisplay_hotswap_gfx_mode( void )
{
  return 0;
}

char*
utils_safe_strdup( const char *src GCC_UNUSED )
{
  return NULL;
}

static int
scalerexpandtest_expect( int x, int y, int w, int h, int expected_x,
                         int expected_y, int expected_w, int expected_h )
{
  if( x == expected_x && y == expected_y && w == expected_w &&
      h == expected_h )
    return 0;

  fprintf( stderr, "got (%d, %d, %d, %d), expected (%d, %d, %d, %d)\n",
           x, y, w, h, expected_x, expected_y, expected_w, expected_h );
  return 1;
}

static int
scalerexpandtest_paltv( void )
{
  scaler_expand_fn *expander = scaler_get_expander( SCALER_PALTV2X );
  int x = 12, y = 8, w = 3, h = 1;

  if( !expander ) {
    fprintf( stderr, "PAL TV scaler has no dirty-region expander\n" );
    return 1;
  }

  if( scaler_get_flags( SCALER_PALTV2X ) & SCALER_FLAGS_FULL_REFRESH ) {
    fprintf( stderr, "PAL TV scaler unexpectedly requires full refresh\n" );
    return 1;
  }

  expander( &x, &y, &w, &h, 320, 240 );
  if( scalerexpandtest_expect( x, y, w, h, 0, 8, 320, 2 ) ) return 1;

  x = 12; y = 239; w = 3; h = 1;
  expander( &x, &y, &w, &h, 320, 240 );
  return scalerexpandtest_expect( x, y, w, h, 0, 239, 320, 1 );
}

int
main( void )
{
  return scalerexpandtest_paltv();
}
