/* joystick.h: Joystick emulation support
   Copyright (c) 2001-2016 Russell Marks, Philip Kendall
   Copyright (c) 2003 Darren Salt
   Copyright (c) 2015 Stuart Brady

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

#ifndef FUSE_JOYSTICK_H
#define FUSE_JOYSTICK_H

#include "libspectrum.h"
#include "fuse-joystick.h"

joystick_type_t joystick_snapshot_type( libspectrum_joystick type );
/* Optional three outputs (joystick 1, joystick 2, keyboard), borrowed only
   during the snapshot module enable pass. NULL selects legacy handling. */
void joystick_snapshot_set_outputs( const joystick_type_t *outputs );

/* Number of joysticks known about & initialised */
extern int joysticks_supported;

void joystick_register_startup( void );

/* A constant to identify the joystick emulated via the keyboard */
#define JOYSTICK_KEYBOARD 2

extern const char *joystick_name[];
extern const char *joystick_connection[];
#define JOYSTICK_CONN_COUNT 4

typedef enum joystick_button {

  JOYSTICK_BUTTON_LEFT = 0,
  JOYSTICK_BUTTON_RIGHT,
  JOYSTICK_BUTTON_UP,
  JOYSTICK_BUTTON_DOWN,
  JOYSTICK_BUTTON_FIRE,

} joystick_button;

/* Called whenever the (Spectrum) joystick is moved or the fire button
   pressed */
int joystick_press( int which, joystick_button button, int press );
/* Clear direct interface button masks without mapping or UI dispatch.
   Keyboard-mapped joystick effects must be released separately. */
void joystick_clear_interfaces( void );

/* Interface-specific read functions */
libspectrum_byte joystick_kempston_read ( libspectrum_word port,
					  libspectrum_byte *attached );
libspectrum_byte joystick_timex_read ( libspectrum_word port,
				       libspectrum_byte which );
libspectrum_byte joystick_fuller_read ( libspectrum_word port,
					libspectrum_byte *attached );

#endif			/* #ifndef FUSE_JOYSTICK_H */
