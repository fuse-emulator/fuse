/* input.c: generalised input events layer for Fuse
   Copyright (c) 2004-2016 Philip Kendall

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

#include "fuse.h"
#include "input.h"
#include "keyboard.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "settings.h"
#include "snapshot.h"
#include "tape.h"
#include "ui/ui.h"
#include "utils.h"

static int keypress( const input_event_key_t *event,
                     input_dispatch_context context );
static int keyrelease( const input_event_key_t *event );
static int do_joystick( const input_event_joystick_t *joystick_event,
                        int press, input_dispatch_context context );

/* One bounded slot is sufficient: any deferred action is an ordering barrier.
   Frontends must retain all subsequent events until normal dispatch resumes. */
static struct {
  enum { ACTION_NONE, ACTION_POPUP, ACTION_KEYPRESS } type;
  input_event_key_t key;
} deferred_action;
static int executing_deferred;
static int (*popup_filter)( input_key native_key );

void
input_dispatch_set_popup_filter( int (*filter)( input_key native_key ) )
{
  popup_filter = filter;
}

int
input_dispatch_pending( void )
{
  return deferred_action.type != ACTION_NONE || executing_deferred;
}

void
input_dispatch_deferred( void )
{
  input_event_key_t key;
  int type;

  if( executing_deferred || deferred_action.type == ACTION_NONE ) return;
  type = deferred_action.type;
  key = deferred_action.key;
  deferred_action.type = ACTION_NONE;
  executing_deferred = 1;
  if( type == ACTION_POPUP ) {
    ui_popup_menu( key.native_key );
  } else {
    /* Escape's mouse-release decision precedes all physical effects. None
       have been applied for this action, so normal handling is safe here. */
    keypress( &key, INPUT_DISPATCH_NORMAL );
  }
  executing_deferred = 0;
}

static int
popup_menu( input_key native_key, input_dispatch_context context )
{
  if( context == INPUT_DISPATCH_NORMAL ) {
    ui_popup_menu( native_key );
    return INPUT_DISPATCH_APPLIED;
  }
  if( popup_filter && !popup_filter( native_key ) ) return INPUT_DISPATCH_APPLIED;
  deferred_action.type = ACTION_POPUP;
  deferred_action.key.native_key = native_key;
  deferred_action.key.spectrum_key = INPUT_KEY_NONE;
  return INPUT_DISPATCH_DEFERRED;
}

static int
joystick_output_valid( int which )
{
  int output;
  switch( which ) {
  case 0: output = settings_current.joystick_1_output; break;
  case 1: output = settings_current.joystick_2_output; break;
  case JOYSTICK_KEYBOARD: output = settings_current.joystick_keyboard_output; break;
  default: return 0;
  }
  return output >= JOYSTICK_TYPE_NONE && output < JOYSTICK_TYPE_COUNT;
}

int
input_event( const input_event_t *event )
{
  return input_event_dispatch( event, INPUT_DISPATCH_NORMAL );
}

input_dispatch_result
input_event_dispatch( const input_event_t *event, input_dispatch_context context )
{
  if( context == INPUT_DISPATCH_READ_TIME ) {
    if( input_dispatch_pending() || ui_widget_level >= 0 || fuse_emulation_paused )
      return INPUT_DISPATCH_BLOCKED;
    /* Avoid the ordinary diagnostic/UI paths on malformed input/settings. */
    if( event->type == INPUT_EVENT_KEYPRESS ||
        event->type == INPUT_EVENT_KEYRELEASE ) {
      if( !joystick_output_valid( JOYSTICK_KEYBOARD ) ) return INPUT_DISPATCH_ERROR;
    } else if( event->type == INPUT_EVENT_JOYSTICK_PRESS ||
               event->type == INPUT_EVENT_JOYSTICK_RELEASE ) {
      const input_event_joystick_t *j = &event->types.joystick;
      if( j->which < 0 || j->which > 1 || j->button < INPUT_JOYSTICK_UP ||
          j->button > INPUT_JOYSTICK_FIRE_15 || !joystick_output_valid( j->which ) )
        return INPUT_DISPATCH_ERROR;
    }
  } else if( context == INPUT_DISPATCH_NORMAL ) {
    input_dispatch_deferred();
  } else {
    return INPUT_DISPATCH_ERROR;
  }

  switch( event->type ) {
  case INPUT_EVENT_KEYPRESS: return keypress( &event->types.key, context );
  case INPUT_EVENT_KEYRELEASE: return keyrelease( &event->types.key );
  case INPUT_EVENT_JOYSTICK_PRESS:
    return do_joystick( &event->types.joystick, 1, context );
  case INPUT_EVENT_JOYSTICK_RELEASE:
    return do_joystick( &event->types.joystick, 0, context );
  }

  if( context == INPUT_DISPATCH_NORMAL )
    ui_error( UI_ERROR_ERROR, "unknown input event type %d", event->type );
  return INPUT_DISPATCH_ERROR;
}

static int
use_shifted_arrow_keys( input_key keysym )
{
  return ( settings_current.keyboard_arrows_shifted &&
           ( keysym == INPUT_KEY_Up || keysym == INPUT_KEY_Down ||
             keysym == INPUT_KEY_Left || keysym == INPUT_KEY_Right ) );
}

/* Track which shifted-arrow keys are currently held, so that Caps Shift
   is only released when the last one goes up.  With the previous code,
   holding two cursor keys and releasing one dropped Caps Shift while the
   other cursor key was still held, leaving the remaining key producing a
   digit instead of cursor movement (bug #470).  A bitmask (rather than a
   count) also keeps repeated keypress events (GTK key repeat) idempotent. */
static int shifted_arrow_keys_held = 0;

/* One bit per cursor key: Up=1, Down=2, Left=4, Right=8 */
static int
shifted_arrow_bit( input_key keysym )
{
  switch( keysym ) {
  case INPUT_KEY_Up:    return 0x01;
  case INPUT_KEY_Down:  return 0x02;
  case INPUT_KEY_Left:  return 0x04;
  case INPUT_KEY_Right: return 0x08;
  default: return 0;
  }
}

void
input_reset_shifted_arrows( void )
{
  shifted_arrow_keys_held = 0;
}

static void
send_keyboard_press( input_key keysym )
{
  const keyboard_spectrum_keys_t *ptr;
  int bit;

  ptr = keyboard_get_spectrum_keys( keysym );

  if( ptr ) {
    keyboard_press( ptr->key1 );
    keyboard_press( ptr->key2 );
  }

  bit = use_shifted_arrow_keys( keysym ) ? shifted_arrow_bit( keysym ) : 0;
  if( bit && !( shifted_arrow_keys_held & bit ) ) {
    shifted_arrow_keys_held |= bit;
    keyboard_press( KEYBOARD_Caps );
  }
}

static void
send_keyboard_release( input_key keysym )
{
  const keyboard_spectrum_keys_t *ptr;
  int bit;

  ptr = keyboard_get_spectrum_keys( keysym );

  if( ptr ) {
    keyboard_release( ptr->key1 );
    keyboard_release( ptr->key2 );
  }

  bit = use_shifted_arrow_keys( keysym ) ? shifted_arrow_bit( keysym ) : 0;
  if( bit && ( shifted_arrow_keys_held & bit ) ) {
    shifted_arrow_keys_held &= ~bit;
    if( shifted_arrow_keys_held == 0 ) keyboard_release( KEYBOARD_Caps );
  }
}

static input_key
recreated_is_downkey( int code )
{
  switch( code ) {

  case INPUT_KEY_a:                                  return INPUT_KEY_1;
  case INPUT_KEY_c:                                  return INPUT_KEY_2;
  case INPUT_KEY_e:                                  return INPUT_KEY_3;
  case INPUT_KEY_g:                                  return INPUT_KEY_4;
  case INPUT_KEY_i:                                  return INPUT_KEY_5;
  case INPUT_KEY_k:                                  return INPUT_KEY_6;
  case INPUT_KEY_m:                                  return INPUT_KEY_7;
  case INPUT_KEY_o:                                  return INPUT_KEY_8;
  case INPUT_KEY_q:                                  return INPUT_KEY_9;
  case INPUT_KEY_s:                                  return INPUT_KEY_0;
  case INPUT_KEY_u:                                  return INPUT_KEY_q;
  case INPUT_KEY_w:                                  return INPUT_KEY_w;
  case INPUT_KEY_y:                                  return INPUT_KEY_e;
  case INPUT_KEY_Shift_L | INPUT_KEY_a:              return INPUT_KEY_r;
  case INPUT_KEY_Shift_L | INPUT_KEY_c:              return INPUT_KEY_t;
  case INPUT_KEY_Shift_L | INPUT_KEY_e:              return INPUT_KEY_y;
  case INPUT_KEY_Shift_L | INPUT_KEY_g:              return INPUT_KEY_u;
  case INPUT_KEY_Shift_L | INPUT_KEY_i:              return INPUT_KEY_i;
  case INPUT_KEY_Shift_L | INPUT_KEY_k:              return INPUT_KEY_o;
  case INPUT_KEY_Shift_L | INPUT_KEY_m:              return INPUT_KEY_p;
  case INPUT_KEY_Shift_L | INPUT_KEY_o:              return INPUT_KEY_a;
  case INPUT_KEY_Shift_L | INPUT_KEY_q:              return INPUT_KEY_s;
  case INPUT_KEY_Shift_L | INPUT_KEY_s:              return INPUT_KEY_d;
  case INPUT_KEY_Shift_L | INPUT_KEY_u:              return INPUT_KEY_f;
  case INPUT_KEY_Shift_L | INPUT_KEY_w:              return INPUT_KEY_g;
  case INPUT_KEY_Shift_L | INPUT_KEY_y:              return INPUT_KEY_h;
  case INPUT_KEY_0:                                  return INPUT_KEY_j;
  case INPUT_KEY_2:                                  return INPUT_KEY_k;
  case INPUT_KEY_4:                                  return INPUT_KEY_l;
  case INPUT_KEY_6:                                  return INPUT_KEY_Return;
  case INPUT_KEY_8:                                  return INPUT_KEY_Shift_L;
  case INPUT_KEY_Shift_L | INPUT_KEY_comma:          return INPUT_KEY_z;
  case INPUT_KEY_minus:                              return INPUT_KEY_x;
  case INPUT_KEY_bracketleft:                        return INPUT_KEY_c;
  case INPUT_KEY_semicolon:                          return INPUT_KEY_v;
  case INPUT_KEY_comma:                              return INPUT_KEY_b;
  case INPUT_KEY_slash:                              return INPUT_KEY_n;
  case INPUT_KEY_Shift_L | INPUT_KEY_bracketleft:    return INPUT_KEY_m;
  case INPUT_KEY_Shift_L | INPUT_KEY_1:              return INPUT_KEY_Control_R;
  case INPUT_KEY_Shift_L | INPUT_KEY_5:              return INPUT_KEY_space;

  }

  return INPUT_KEY_NONE;
}

static input_key
recreated_is_upkey( int code )
{
  switch( code ) {

  case INPUT_KEY_b:                                  return INPUT_KEY_1;
  case INPUT_KEY_d:                                  return INPUT_KEY_2;
  case INPUT_KEY_f:                                  return INPUT_KEY_3;
  case INPUT_KEY_h:                                  return INPUT_KEY_4;
  case INPUT_KEY_j:                                  return INPUT_KEY_5;
  case INPUT_KEY_l:                                  return INPUT_KEY_6;
  case INPUT_KEY_n:                                  return INPUT_KEY_7;
  case INPUT_KEY_p:                                  return INPUT_KEY_8;
  case INPUT_KEY_r:                                  return INPUT_KEY_9;
  case INPUT_KEY_t:                                  return INPUT_KEY_0;
  case INPUT_KEY_v:                                  return INPUT_KEY_q;
  case INPUT_KEY_x:                                  return INPUT_KEY_w;
  case INPUT_KEY_z:                                  return INPUT_KEY_e;
  case INPUT_KEY_Shift_L | INPUT_KEY_b:              return INPUT_KEY_r;
  case INPUT_KEY_Shift_L | INPUT_KEY_d:              return INPUT_KEY_t;
  case INPUT_KEY_Shift_L | INPUT_KEY_f:              return INPUT_KEY_y;
  case INPUT_KEY_Shift_L | INPUT_KEY_h:              return INPUT_KEY_u;
  case INPUT_KEY_Shift_L | INPUT_KEY_j:              return INPUT_KEY_i;
  case INPUT_KEY_Shift_L | INPUT_KEY_l:              return INPUT_KEY_o;
  case INPUT_KEY_Shift_L | INPUT_KEY_n:              return INPUT_KEY_p;
  case INPUT_KEY_Shift_L | INPUT_KEY_p:              return INPUT_KEY_a;
  case INPUT_KEY_Shift_L | INPUT_KEY_r:              return INPUT_KEY_s;
  case INPUT_KEY_Shift_L | INPUT_KEY_t:              return INPUT_KEY_d;
  case INPUT_KEY_Shift_L | INPUT_KEY_v:              return INPUT_KEY_f;
  case INPUT_KEY_Shift_L | INPUT_KEY_x:              return INPUT_KEY_g;
  case INPUT_KEY_Shift_L | INPUT_KEY_z:              return INPUT_KEY_h;
  case INPUT_KEY_1:                                  return INPUT_KEY_j;
  case INPUT_KEY_3:                                  return INPUT_KEY_k;
  case INPUT_KEY_5:                                  return INPUT_KEY_l;
  case INPUT_KEY_7:                                  return INPUT_KEY_Return;
  case INPUT_KEY_9:                                  return INPUT_KEY_Shift_L;
  case INPUT_KEY_Shift_L | INPUT_KEY_period:         return INPUT_KEY_z;
  case INPUT_KEY_equal:                              return INPUT_KEY_x;
  case INPUT_KEY_bracketright:                       return INPUT_KEY_c;
  case INPUT_KEY_Shift_L | INPUT_KEY_semicolon:      return INPUT_KEY_v;
  case INPUT_KEY_period:                             return INPUT_KEY_b;
  case INPUT_KEY_Shift_L | INPUT_KEY_slash:          return INPUT_KEY_n;
  case INPUT_KEY_Shift_L | INPUT_KEY_bracketright:   return INPUT_KEY_m;
  case INPUT_KEY_Shift_L | INPUT_KEY_4:              return INPUT_KEY_Control_R;
  case INPUT_KEY_Shift_L | INPUT_KEY_6:              return INPUT_KEY_space;

  }

  return INPUT_KEY_NONE;
}

static int recreated_key_down = 0;

void
input_cancel_physical( void )
{
  keyboard_release_all();
  recreated_key_down = 0;
  /* Clear every interface, even one whose configured output has changed.
     Never pass cancellation through joystick-to-menu or widget routing. */
  joystick_clear_interfaces();
}

static void
recreated_keypress( input_key k )
{
  input_key o;    /* remapped key */

  if( k == INPUT_KEY_Shift_L )
    recreated_key_down |= INPUT_KEY_Shift_L;

  if( k >= 0 && k < 256 )
    recreated_key_down = ( recreated_key_down & ~255 ) | k;

  o = recreated_is_upkey( recreated_key_down );
  if( o ) {
    send_keyboard_release( o );
    recreated_key_down = 0;
    return;
  }

  o = recreated_is_downkey( recreated_key_down );
  if( o ) {
    send_keyboard_press( o );
    recreated_key_down = 0;
    return;
  }
}

static int
keypress( const input_event_key_t *event, input_dispatch_context context )
{
  int swallow;

  if( ui_widget_level >= 0 ) {
    ui_widget_keyhandler( event->native_key );
    return 0;
  }

  /* Escape => ask UI to end mouse grab, return if grab ended */
  if( event->native_key == INPUT_KEY_Escape && ui_mouse_grabbed ) {
    if( context == INPUT_DISPATCH_READ_TIME ) {
      deferred_action.type = ACTION_KEYPRESS;
      deferred_action.key = *event;
      return INPUT_DISPATCH_DEFERRED;
    }
    ui_mouse_grabbed = ui_mouse_release( 0 );
    if( !ui_mouse_grabbed ) return 0;
  }

  swallow = input_keypress_emulation( event->spectrum_key );
  return swallow ? 0 : popup_menu( event->native_key, context );
}

int
input_keypress_emulation( input_key key )
{
  int swallow = 0;
  /* Joystick emulation via keyboard keys */
  if( key == settings_current.joystick_keyboard_up )
    swallow = joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_UP, 1 );
  else if( key == settings_current.joystick_keyboard_down )
    swallow = joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_DOWN, 1 );
  else if( key == settings_current.joystick_keyboard_left )
    swallow = joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_LEFT, 1 );
  else if( key == settings_current.joystick_keyboard_right )
    swallow = joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_RIGHT, 1 );
  else if( key == settings_current.joystick_keyboard_fire )
    swallow = joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_FIRE, 1 );
  if( swallow ) return 1;
  if( settings_current.recreated_spectrum ) recreated_keypress( key );
  else send_keyboard_press( key );
  return 0;
}

static int
keyrelease( const input_event_key_t *event )
{
  input_keyrelease_emulation( event->spectrum_key );
  return 0;
}

void
input_keyrelease_emulation( input_key key )
{
  if( !settings_current.recreated_spectrum ) send_keyboard_release( key );
  if( key == settings_current.joystick_keyboard_up )
    joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_UP, 0 );
  else if( key == settings_current.joystick_keyboard_down )
    joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_DOWN, 0 );
  else if( key == settings_current.joystick_keyboard_left )
    joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_LEFT, 0 );
  else if( key == settings_current.joystick_keyboard_right )
    joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_RIGHT, 0 );
  else if( key == settings_current.joystick_keyboard_fire )
    joystick_press( JOYSTICK_KEYBOARD, JOYSTICK_BUTTON_FIRE, 0 );
}

void
input_mouse_button_event( input_mouse_button_t button, int pressed )
{
  int left = !settings_current.mouse_swap_buttons;
  kempmouse_update( 0, 0, button == INPUT_MOUSE_LEFT ? left : !left, pressed );
}

static keyboard_key_name
get_fire_button_key( int which, input_key button )
{
  switch( which ) {

  case 0:
    switch( button ) {
    case INPUT_JOYSTICK_FIRE_1 : return settings_current.joystick_1_fire_1;
    case INPUT_JOYSTICK_FIRE_2 : return settings_current.joystick_1_fire_2;
    case INPUT_JOYSTICK_FIRE_3 : return settings_current.joystick_1_fire_3;
    case INPUT_JOYSTICK_FIRE_4 : return settings_current.joystick_1_fire_4;
    case INPUT_JOYSTICK_FIRE_5 : return settings_current.joystick_1_fire_5;
    case INPUT_JOYSTICK_FIRE_6 : return settings_current.joystick_1_fire_6;
    case INPUT_JOYSTICK_FIRE_7 : return settings_current.joystick_1_fire_7;
    case INPUT_JOYSTICK_FIRE_8 : return settings_current.joystick_1_fire_8;
    case INPUT_JOYSTICK_FIRE_9 : return settings_current.joystick_1_fire_9;
    case INPUT_JOYSTICK_FIRE_10: return settings_current.joystick_1_fire_10;
    case INPUT_JOYSTICK_FIRE_11: return settings_current.joystick_1_fire_11;
    case INPUT_JOYSTICK_FIRE_12: return settings_current.joystick_1_fire_12;
    case INPUT_JOYSTICK_FIRE_13: return settings_current.joystick_1_fire_13;
    case INPUT_JOYSTICK_FIRE_14: return settings_current.joystick_1_fire_14;
    case INPUT_JOYSTICK_FIRE_15: return settings_current.joystick_1_fire_15;
    default: break;
    }
    break;

  case 1:
    switch( button ) {
    case INPUT_JOYSTICK_FIRE_1 : return settings_current.joystick_2_fire_1;
    case INPUT_JOYSTICK_FIRE_2 : return settings_current.joystick_2_fire_2;
    case INPUT_JOYSTICK_FIRE_3 : return settings_current.joystick_2_fire_3;
    case INPUT_JOYSTICK_FIRE_4 : return settings_current.joystick_2_fire_4;
    case INPUT_JOYSTICK_FIRE_5 : return settings_current.joystick_2_fire_5;
    case INPUT_JOYSTICK_FIRE_6 : return settings_current.joystick_2_fire_6;
    case INPUT_JOYSTICK_FIRE_7 : return settings_current.joystick_2_fire_7;
    case INPUT_JOYSTICK_FIRE_8 : return settings_current.joystick_2_fire_8;
    case INPUT_JOYSTICK_FIRE_9 : return settings_current.joystick_2_fire_9;
    case INPUT_JOYSTICK_FIRE_10: return settings_current.joystick_2_fire_10;
    case INPUT_JOYSTICK_FIRE_11: return settings_current.joystick_2_fire_11;
    case INPUT_JOYSTICK_FIRE_12: return settings_current.joystick_2_fire_12;
    case INPUT_JOYSTICK_FIRE_13: return settings_current.joystick_2_fire_13;
    case INPUT_JOYSTICK_FIRE_14: return settings_current.joystick_2_fire_14;
    case INPUT_JOYSTICK_FIRE_15: return settings_current.joystick_2_fire_15;
    default: break;
    }
    break;

  }

  ui_error( UI_ERROR_ERROR, "get_fire_button_key: which = %d, button = %d",
	    which, button );
  fuse_abort();
}

/* A fire button mapped to the menu key opens the main menu on widget UIs,
   except on the Wii where the home button already opens the menu; on UIs
   without a widget menu, and on the Wii, the menu key falls back to a plain
   joystick fire button to preserve the previous behaviour */
static int
do_menu_key( int which, int press, input_dispatch_context context GCC_UNUSED )
{
#ifndef GEKKO /* Home button opens the menu on Wii */
  if( ui_has_widget_menu ) {
    if( press ) return popup_menu( INPUT_KEY_F1, context );
    return INPUT_DISPATCH_APPLIED;
  }
#endif

  joystick_press( which, JOYSTICK_BUTTON_FIRE, press );
  return INPUT_DISPATCH_APPLIED;
}

static int
do_joystick( const input_event_joystick_t *joystick_event, int press,
             input_dispatch_context context )
{
  int which;

  if( ui_has_widget_menu && ui_widget_level >= 0 ) {
    if( press ) ui_widget_keyhandler( joystick_event->button );
    return 0;
  }

  which = joystick_event->which;

  if( joystick_event->button < INPUT_JOYSTICK_FIRE_1 ) {

    joystick_button button;

    switch( joystick_event->button ) {
    case INPUT_JOYSTICK_UP   : button = JOYSTICK_BUTTON_UP;    break;
    case INPUT_JOYSTICK_DOWN : button = JOYSTICK_BUTTON_DOWN;  break;
    case INPUT_JOYSTICK_LEFT : button = JOYSTICK_BUTTON_LEFT;  break;
    case INPUT_JOYSTICK_RIGHT: button = JOYSTICK_BUTTON_RIGHT; break;

    default:
      ui_error( UI_ERROR_ERROR, "do_joystick: unknown button %d",
		joystick_event->button );
      fuse_abort();
    }

    joystick_press( which, button, press );

  } else {

    keyboard_key_name key;

    key = get_fire_button_key( which, joystick_event->button );

    if( key == KEYBOARD_JOYSTICK_FIRE ) {
      joystick_press( which, JOYSTICK_BUTTON_FIRE, press );
    } else if( key == KEYBOARD_MENU ) {
      return do_menu_key( which, press, context );
    } else {
      if( press ) {
	keyboard_press( key );
      } else {
	keyboard_release( key );
      }
    }

  }

  return 0;
}
