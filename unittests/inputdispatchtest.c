/* Shared mapping/deferred UI tests, using real input.c with UI/device spies. */
#include "config.h"

/* Test checks must execute even when the emulator is built with NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuse.h"
#include "input.h"
#include "keyboard.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "settings.h"
#include "ui/ui.h"

settings_info settings_current;
int ui_widget_level = -1, ui_mouse_grabbed, fuse_emulation_paused;
#ifdef USE_WIDGET
const int ui_has_widget_menu = 1;
#else
const int ui_has_widget_menu = 0;
#endif
static int physical[0x200], joystick_down[3][5];
static int presses, releases, popups, widgets, mouse_calls, errors;
static int last_popup, mouse_result, recursive_popup;
static int emulated_mouse_button, emulated_mouse_pressed;
static char order[1024];
static int order_count;

static void log_effect( char c )
{
  assert( order_count < (int)sizeof( order ) - 1 );
  order[order_count++] = c;
  order[order_count] = 0;
}

static input_event_t key_event( input_key key, int press )
{
  input_event_t event;
  event.type = press ? INPUT_EVENT_KEYPRESS : INPUT_EVENT_KEYRELEASE;
  event.types.key.native_key = event.types.key.spectrum_key = key;
  return event;
}

static input_event_t joy_event( input_key button, int press )
{
  input_event_t event;
  event.type = press ? INPUT_EVENT_JOYSTICK_PRESS : INPUT_EVENT_JOYSTICK_RELEASE;
  event.types.joystick.which = 0;
  event.types.joystick.button = button;
  return event;
}

const keyboard_spectrum_keys_t *keyboard_get_spectrum_keys( input_key key )
{
  static const struct {
    input_key input;
    keyboard_spectrum_keys_t keys;
  } mapping[] = {
    { INPUT_KEY_a, { KEYBOARD_a, KEYBOARD_NONE } },
    { INPUT_KEY_b, { KEYBOARD_b, KEYBOARD_NONE } },
    { INPUT_KEY_q, { KEYBOARD_q, KEYBOARD_NONE } },
    { INPUT_KEY_1, { KEYBOARD_1, KEYBOARD_NONE } },
    { INPUT_KEY_Shift_L, { KEYBOARD_NONE, KEYBOARD_Caps } },
    { INPUT_KEY_Control_R, { KEYBOARD_NONE, KEYBOARD_Symbol } },
    { INPUT_KEY_Left, { KEYBOARD_5, KEYBOARD_NONE } },
    { INPUT_KEY_Right, { KEYBOARD_8, KEYBOARD_NONE } },
    { INPUT_KEY_Escape, { KEYBOARD_1, KEYBOARD_Caps } }
  };
  unsigned int i;
  for( i = 0; i < sizeof( mapping ) / sizeof( mapping[0] ); i++ )
    if( mapping[i].input == key ) return &mapping[i].keys;
  return NULL;
}

void keyboard_press( keyboard_key_name key )
{
  if( key == KEYBOARD_NONE ) return;
  assert( key < 0x200 );
  physical[key] = 1; presses++;
  log_effect( 'P' );
}

void keyboard_release( keyboard_key_name key )
{
  if( key == KEYBOARD_NONE ) return;
  assert( key < 0x200 );
  physical[key] = 0; releases++;
  log_effect( 'R' );
}

int keyboard_release_all( void )
{
  memset( physical, 0, sizeof( physical ) );
  input_reset_shifted_arrows();
  return 0;
}

int joystick_press( int which, joystick_button button, int press )
{
  int output;
  assert( which >= 0 && which <= 2 && button >= 0 && button < 5 );
  output = which == 0 ? settings_current.joystick_1_output :
           which == 1 ? settings_current.joystick_2_output :
                        settings_current.joystick_keyboard_output;
  if( output == JOYSTICK_TYPE_NONE ) return 0;
  assert( output > 0 && output < JOYSTICK_TYPE_COUNT );
  joystick_down[which][button] = press;
  log_effect( press ? 'J' : 'j' );
  return 1;
}

void kempmouse_update( int dx, int dy, int button, int pressed )
{
  assert( dx == 0 && dy == 0 );
  emulated_mouse_button = button;
  emulated_mouse_pressed = pressed;
}

void joystick_clear_interfaces( void )
{
  memset( joystick_down, 0, sizeof( joystick_down ) );
}

void ui_popup_menu( int native_key )
{
  input_event_t event;
  popups++; last_popup = native_key;
  log_effect( 'U' );
  if( recursive_popup ) {
    recursive_popup = 0;
    assert( input_dispatch_pending() );
    input_dispatch_deferred(); /* Removed before callback; cannot run twice. */
    event = key_event( INPUT_KEY_b, 1 );
    assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
            INPUT_DISPATCH_BLOCKED );
    ui_widget_level = 0;
    assert( input_event( &event ) == 0 ); /* Nested normal widget dispatch works. */
    ui_widget_level = -1;
  }
}

void ui_widget_keyhandler( int key GCC_UNUSED )
{
  widgets++;
  log_effect( 'W' );
}

int ui_mouse_release( int suspend GCC_UNUSED )
{
  mouse_calls++;
  log_effect( 'M' );
  return mouse_result;
}

int ui_error( ui_error_level severity GCC_UNUSED, const char *format GCC_UNUSED, ... )
{
  errors++;
  return 0;
}

void fuse_abort( void ) { abort(); }

static void reset( void )
{
  assert( !input_dispatch_pending() );
  memset( &settings_current, 0, sizeof( settings_current ) );
  settings_current.joystick_keyboard_up = INPUT_KEY_NONE;
  settings_current.joystick_keyboard_down = INPUT_KEY_NONE;
  settings_current.joystick_keyboard_left = INPUT_KEY_NONE;
  settings_current.joystick_keyboard_right = INPUT_KEY_NONE;
  settings_current.joystick_keyboard_fire = INPUT_KEY_NONE;
  ui_widget_level = -1; ui_mouse_grabbed = fuse_emulation_paused = 0;
  recursive_popup = 0; mouse_result = 0;
  input_dispatch_set_popup_filter( NULL );
  input_cancel_physical();
  memset( joystick_down, 0, sizeof( joystick_down ) );
  presses = releases = popups = widgets = mouse_calls = errors = 0;
  order_count = 0; order[0] = 0;
}

static void send_read( input_key key, int press )
{
  input_event_t event = key_event( key, press );
  input_dispatch_result result = input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME );
  assert( result == INPUT_DISPATCH_APPLIED || result == INPUT_DISPATCH_DEFERRED );
  input_dispatch_deferred();
}

static void test_effects_and_ordering( void )
{
  input_event_t down = key_event( INPUT_KEY_a, 1 );
  input_event_t up = key_event( INPUT_KEY_a, 0 );
  reset();
  assert( input_event_dispatch( &down, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_DEFERRED );
  assert( physical[KEYBOARD_a] && presses == 1 && popups == 0 );
  assert( input_event_dispatch( &up, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_BLOCKED );
  assert( physical[KEYBOARD_a] && releases == 0 );
  assert( input_event( &up ) == 0 ); /* Drain action before new normal effects. */
  assert( strcmp( order, "PUR" ) == 0 );
  assert( !physical[KEYBOARD_a] && presses == 1 && popups == 1 );
  input_dispatch_deferred();
  assert( popups == 1 && !input_dispatch_pending() );
}

static void test_mapping_equivalence( void )
{
  const input_key sequence[] = { INPUT_KEY_Shift_L, INPUT_KEY_a,
    INPUT_KEY_Control_R, INPUT_KEY_Left, INPUT_KEY_Right };
  int normal[0x200], normal_presses, normal_releases;
  unsigned int i;
  reset(); settings_current.keyboard_arrows_shifted = 1;
  for( i = 0; i < sizeof( sequence ) / sizeof( sequence[0] ); i++ ) {
    input_event_t event = key_event( sequence[i], 1 );
    assert( input_event( &event ) == 0 );
  }
  memcpy( normal, physical, sizeof( normal ) ); normal_presses = presses;
  reset(); settings_current.keyboard_arrows_shifted = 1;
  for( i = 0; i < sizeof( sequence ) / sizeof( sequence[0] ); i++ )
    send_read( sequence[i], 1 );
  assert( memcmp( normal, physical, sizeof( normal ) ) == 0 );
  assert( presses == normal_presses );
  send_read( INPUT_KEY_Left, 0 );
  assert( physical[KEYBOARD_Caps] && physical[KEYBOARD_8] );
  send_read( INPUT_KEY_Right, 0 );
  assert( !physical[KEYBOARD_Caps] );
  normal_releases = releases;
  send_read( INPUT_KEY_a, 0 );
  assert( releases == normal_releases + 1 && !physical[KEYBOARD_a] );
}

static void test_joystick_mapping( void )
{
  input_event_t event;
  reset();
  settings_current.joystick_keyboard_up = INPUT_KEY_q;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_KEMPSTON;
  event = key_event( INPUT_KEY_q, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_APPLIED );
  assert( joystick_down[2][JOYSTICK_BUTTON_UP] && !presses && !popups );
  event.type = INPUT_EVENT_KEYRELEASE;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_APPLIED );
  assert( !joystick_down[2][JOYSTICK_BUTTON_UP] );
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  event = joy_event( INPUT_JOYSTICK_LEFT, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_APPLIED );
  assert( joystick_down[0][JOYSTICK_BUTTON_LEFT] );
  settings_current.joystick_1_fire_1 = KEYBOARD_a;
  event = joy_event( INPUT_JOYSTICK_FIRE_1, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_APPLIED );
  assert( physical[KEYBOARD_a] );
  event.type = INPUT_EVENT_JOYSTICK_RELEASE;
  input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME );
  assert( !physical[KEYBOARD_a] );
  settings_current.joystick_1_fire_1 = KEYBOARD_MENU;
  event.type = INPUT_EVENT_JOYSTICK_PRESS;
#if defined(USE_WIDGET) && !defined(GEKKO)
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_DEFERRED );
  assert( !popups );
  input_dispatch_deferred();
  assert( popups == 1 && last_popup == INPUT_KEY_F1 );
#else
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_APPLIED );
  assert( joystick_down[0][JOYSTICK_BUTTON_FIRE] && !popups );
#endif
  event.type = INPUT_EVENT_JOYSTICK_RELEASE;
  {
    int old_popups = popups;
    assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
            INPUT_DISPATCH_APPLIED );
    assert( popups == old_popups && !input_dispatch_pending() );
  }
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  send_read( INPUT_KEY_q, 1 ); /* Unswallowed keyboard mapping still applies. */
  assert( physical[KEYBOARD_q] );
}

static void test_escape_consumption( void )
{
  input_event_t event = key_event( INPUT_KEY_Escape, 1 );
  int keep_grab;
  for( keep_grab = 0; keep_grab <= 1; keep_grab++ ) {
    reset(); ui_mouse_grabbed = 1; mouse_result = keep_grab;
    assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
            INPUT_DISPATCH_DEFERRED );
    assert( !mouse_calls && !presses && !popups && ui_mouse_grabbed );
    input_dispatch_deferred();
    assert( mouse_calls == 1 && ui_mouse_grabbed == keep_grab );
    assert( presses == ( keep_grab ? 2 : 0 ) );
    assert( popups == keep_grab );
    input_dispatch_deferred();
    assert( mouse_calls == 1 );
  }
}

static void test_widgets_and_recursive_drain( void )
{
  input_event_t event = key_event( INPUT_KEY_a, 1 );
  reset(); ui_widget_level = 0;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_BLOCKED );
  assert( !widgets && !presses );
  assert( input_event( &event ) == 0 && widgets == 1 );
  ui_widget_level = -1; fuse_emulation_paused = 1;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_BLOCKED );
  fuse_emulation_paused = 0;
  recursive_popup = 1;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_DEFERRED );
  input_dispatch_deferred();
  assert( popups == 1 && widgets == 2 && !input_dispatch_pending() );
}

static void test_focus_and_recreated( void )
{
  input_event_t event;
  reset(); settings_current.keyboard_arrows_shifted = 1;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_keyboard_up = INPUT_KEY_q;
  send_read( INPUT_KEY_Left, 1 );
  send_read( INPUT_KEY_q, 1 );
  event = key_event( INPUT_KEY_a, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
          INPUT_DISPATCH_DEFERRED );
  joystick_down[0][JOYSTICK_BUTTON_FIRE] = 1;
  ui_widget_level = 0;
  input_cancel_physical(); /* Does not execute the unrelated pending popup. */
  ui_widget_level = -1;
  assert( !joystick_down[0][JOYSTICK_BUTTON_FIRE] && !widgets );
  assert( popups == 1 && input_dispatch_pending() );
  assert( !physical[KEYBOARD_Caps] && !physical[KEYBOARD_a] );
  assert( !joystick_down[2][JOYSTICK_BUTTON_UP] );
  input_dispatch_deferred();
  assert( !physical[KEYBOARD_a] ); /* Already-applied transitions not replayed. */
  send_read( INPUT_KEY_Left, 1 );
  assert( physical[KEYBOARD_Caps] );
  input_cancel_physical();
  settings_current.recreated_spectrum = 1;
  send_read( INPUT_KEY_Shift_L, 1 ); /* Partial Recreated sequence. */
  input_cancel_physical();
  send_read( INPUT_KEY_a, 1 ); /* a means Spectrum 1, not shifted r. */
  assert( physical[KEYBOARD_1] );
  send_read( INPUT_KEY_b, 1 ); /* Recreated release code. */
  assert( !physical[KEYBOARD_1] );
}

static void test_ui_keys_and_invalid_events( void )
{
  input_event_t event;
  const input_key keys[] = { INPUT_KEY_F1, INPUT_KEY_F5, INPUT_KEY_F9, INPUT_KEY_F10 };
  unsigned int i;
  reset();
  for( i = 0; i < sizeof( keys ) / sizeof( keys[0] ); i++ ) {
    event = key_event( keys[i], 1 );
    assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) ==
            INPUT_DISPATCH_DEFERRED );
    assert( popups == (int)i );
    input_dispatch_deferred();
    assert( last_popup == (int)keys[i] && popups == (int)i + 1 );
  }
  event = joy_event( INPUT_JOYSTICK_FIRE_1, 1 );
  event.types.joystick.which = -1;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_ERROR );
  event = joy_event( (input_key)0x7fff, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_ERROR );
  event = key_event( INPUT_KEY_a, 1 );
  settings_current.joystick_keyboard_output = -1;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_ERROR );
  event.type = (input_event_type)99;
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_ERROR );
  assert( !errors );
  assert( input_event( &event ) == 1 && errors == 1 );
}

static int popup_classifier( input_key key )
{
  return key == INPUT_KEY_F5;
}

static void test_popup_classifier( void )
{
  input_event_t event;
  reset(); input_dispatch_set_popup_filter( popup_classifier );
  event = key_event( INPUT_KEY_a, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_APPLIED );
  assert( physical[KEYBOARD_a] && !popups && !input_dispatch_pending() );
  event = key_event( INPUT_KEY_F5, 1 );
  assert( input_event_dispatch( &event, INPUT_DISPATCH_READ_TIME ) == INPUT_DISPATCH_DEFERRED );
  input_dispatch_deferred(); assert( popups == 1 && last_popup == INPUT_KEY_F5 );
  event = key_event( INPUT_KEY_b, 1 );
  assert( input_event( &event ) == 0 && popups == 2 ); /* Normal calls unchanged. */
  input_dispatch_set_popup_filter( NULL );
}

static void test_emulation_only( void )
{
  int swap;
  reset();
  ui_widget_level = 0;
  ui_mouse_grabbed = 1;
  input_keypress_emulation( INPUT_KEY_Escape );
  assert( physical[KEYBOARD_1] && physical[KEYBOARD_Caps] );
  input_keyrelease_emulation( INPUT_KEY_Escape );
  input_keypress_emulation( INPUT_KEY_F1 );
  assert( !popups && !widgets && !mouse_calls && !input_dispatch_pending() );
  for( swap = 0; swap < 2; swap++ ) {
    settings_current.mouse_swap_buttons = swap;
    input_mouse_button_event( INPUT_MOUSE_LEFT, 1 );
    assert( emulated_mouse_button == !swap && emulated_mouse_pressed == 1 );
    input_mouse_button_event( INPUT_MOUSE_RIGHT, 0 );
    assert( emulated_mouse_button == swap && emulated_mouse_pressed == 0 );
  }
  assert( !popups && !widgets && !mouse_calls );
  ui_widget_level = -1;
}

int main( void )
{
  test_emulation_only();
  test_effects_and_ordering();
  test_mapping_equivalence();
  test_joystick_mapping();
  test_escape_consumption();
  test_widgets_and_recursive_drain();
  test_focus_and_recreated();
  test_ui_keys_and_invalid_events();
  test_popup_classifier();
  puts( "input dispatch tests passed" );
  return 0;
}
