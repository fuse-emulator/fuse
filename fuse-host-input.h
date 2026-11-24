/* fuse-host-input.h: opt-in host input and machine controls
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

#ifndef FUSE_HOST_INPUT_H
#define FUSE_HOST_INPUT_H

#include "fuse-host.h"
#include "fuse-joystick.h"
#include "input.h"

typedef enum fuse_host_reset_kind {
  FUSE_HOST_RESET_SOFT, FUSE_HOST_RESET_HARD
} fuse_host_reset_kind;

/* Logical input slots. Fuse owns their configured emulated-interface mapping. */
typedef enum fuse_host_joystick {
  FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_2, FUSE_HOST_JOYSTICK_KEYBOARD
} fuse_host_joystick;

/* Engine interface selection, deliberately separate from input delivery. */
int fuse_host_config_set_joystick( fuse_host_config *config,
                                   fuse_host_joystick slot,
                                   joystick_type_t output );

int fuse_host_config_set_mouse_swap_buttons( fuse_host_config *config,
                                             int swap );

typedef enum fuse_host_joystick_button {
  FUSE_HOST_JOYSTICK_LEFT, FUSE_HOST_JOYSTICK_RIGHT, FUSE_HOST_JOYSTICK_UP,
  FUSE_HOST_JOYSTICK_DOWN, FUSE_HOST_JOYSTICK_FIRE
} fuse_host_joystick_button;

typedef enum fuse_host_mouse_button {
  FUSE_HOST_MOUSE_LEFT, FUSE_HOST_MOUSE_RIGHT
} fuse_host_mouse_button;

fuse_host_operation_result fuse_host_select_machine( const char *id );
fuse_host_operation_result fuse_host_reset( fuse_host_reset_kind kind );
fuse_host_operation_result fuse_host_nmi( void );

/* Normalized keyboard values use shared Fuse translation and keyboard-joystick
   mapping, but never trigger menus/widgets/grab actions (even for F-keys/Escape).
   These are not toolkit keycodes. Direct matrix injection is a separate option. */
fuse_host_operation_result fuse_host_keyboard_event( input_key key,
                                                     int pressed );
fuse_host_operation_result fuse_host_joystick_event(
  fuse_host_joystick joystick,
  fuse_host_joystick_button
  button, int pressed );
/* Relative motion in host-style coordinates (positive y down), wrapping the
   hardware's 8-bit counters. Logical buttons respect Fuse's swap configuration;
   pointer capture and other native UI actions remain host policy. */
fuse_host_operation_result fuse_host_mouse_motion( int dx, int dy );
fuse_host_operation_result fuse_host_mouse_event( fuse_host_mouse_button button,
                                                  int pressed );
/* Clears physical keyboard/joystick/mouse buttons, retaining mouse position and
   synthetic typing. Select/reset do not add an implicit release: the host sends
   release-all explicitly for focus loss, retaining existing reset semantics. */
fuse_host_operation_result fuse_host_release_all( void );

#endif
