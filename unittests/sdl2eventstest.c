/* Real SDL queue tests; no window, renderer, audio or host input required. */
#include "config.h"

/* Test checks must execute even when the emulator is built with NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/sdl2/sdl2_events.h"

static sdl2_event_collector collector;
static int action, executing, ui_calls, physical, cancellations, cancelled_events;
static int nested_menu, widget, recurse, reject;
static char order[512];
static int length;

static void log_event( char c ) { assert( length < 511 ); order[length++] = c; order[length] = 0; }
int input_dispatch_pending( void ) { return action || executing; }
void input_dispatch_deferred( void )
{
  if( executing || !action ) return;
  action = 0; executing = 1; ui_calls++; log_event( 'U' );
  if( nested_menu ) {
    nested_menu = 0; widget = 1;
    assert( sdl2_events_collect( &collector, INPUT_DISPATCH_NORMAL ) == INPUT_REFRESH_SUCCESS );
    widget = 0;
  }
  executing = 0;
}

static input_dispatch_result dispatch( SDL_Event *event, input_dispatch_context context,
                                       int cancelled, void *data )
{
  assert( data == &collector );
  if( recurse ) {
    recurse = 0;
    assert( sdl2_events_collect( &collector, INPUT_DISPATCH_READ_TIME ) == INPUT_REFRESH_SKIPPED );
    if( context == INPUT_DISPATCH_READ_TIME )
      assert( sdl2_events_collect( &collector, INPUT_DISPATCH_NORMAL ) == INPUT_REFRESH_SKIPPED );
  }
  if( event->type == SDL_KEYDOWN || event->type == SDL_KEYUP ) {
    if( context == INPUT_DISPATCH_READ_TIME && reject ) return INPUT_DISPATCH_ERROR;
    if( event->key.keysym.sym == SDLK_F1 && context == INPUT_DISPATCH_READ_TIME ) {
      action = 1; log_event( 'f' ); return INPUT_DISPATCH_DEFERRED;
    }
    if( widget ) { ui_calls++; log_event( 'w' ); }
    else { physical++; log_event( (char)event->key.keysym.sym ); }
  } else {
    assert( context == INPUT_DISPATCH_NORMAL );
    ui_calls++;
    if( event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ) {
      cancelled_events += cancelled;
      if( !cancelled ) { cancellations++; physical = 0; }
      log_event( 'l' );
    } else if( event->type == SDL_WINDOWEVENT ) log_event( 'g' );
    else log_event( 'm' );
  }
  return INPUT_DISPATCH_APPLIED;
}

static void cancel_focus( void *data )
{
  assert( data == &collector );
  cancellations++; physical = 0; log_event( 'C' );
}

static void reset( void )
{
  sdl2_events_end( &collector );
  SDL_FlushEvents( SDL_FIRSTEVENT, SDL_LASTEVENT );
  sdl2_events_init( &collector, dispatch, cancel_focus, &collector );
  action = executing = ui_calls = physical = cancellations = cancelled_events = 0;
  nested_menu = widget = recurse = reject = length = 0; order[0] = 0;
}

static void key( SDL_Keycode sym )
{
  SDL_Event event;
  memset( &event, 0, sizeof( event ) );
  event.type = SDL_KEYDOWN; event.key.keysym.sym = sym;
  assert( SDL_PushEvent( &event ) == 1 );
}

static void other( Uint32 type, Uint8 detail )
{
  SDL_Event event;
  memset( &event, 0, sizeof( event ) ); event.type = type;
  if( type == SDL_WINDOWEVENT ) event.window.event = detail;
  assert( SDL_PushEvent( &event ) == 1 );
}

static input_refresh_result read_events( void )
{
  return sdl2_events_collect( &collector, INPUT_DISPATCH_READ_TIME );
}

static void normal_events( void )
{
  assert( sdl2_events_collect( &collector, INPUT_DISPATCH_NORMAL ) == INPUT_REFRESH_SUCCESS );
}

int main( int argc, char **argv )
{
  int i;
  SDL_Event event;
  (void)argc;
  (void)argv;
  assert( SDL_Init( SDL_INIT_EVENTS ) == 0 );
  reset();
  assert( read_events() == INPUT_REFRESH_SUCCESS ); /* Empty success. */
  key( SDLK_a ); key( SDLK_b );
  assert( read_events() == INPUT_REFRESH_SUCCESS );
  assert( !strcmp( order, "ab" ) && !ui_calls );

  reset(); key( SDLK_a ); other( SDL_MOUSEBUTTONDOWN, 0 ); key( SDLK_b );
  assert( read_events() == INPUT_REFRESH_SKIPPED );
  assert( !strcmp( order, "a" ) && collector.has_pending );
  assert( read_events() == INPUT_REFRESH_SKIPPED );
  normal_events();
  assert( !strcmp( order, "amb" ) && physical == 2 && ui_calls == 1 );

  reset(); key( SDLK_F1 ); key( SDLK_b ); nested_menu = 1;
  assert( read_events() == INPUT_REFRESH_SKIPPED && action );
  assert( read_events() == INPUT_REFRESH_SKIPPED );
  normal_events();
  assert( !strcmp( order, "fUw" ) && !physical && ui_calls == 2 );
  assert( !action && !executing );

  reset(); physical = 1;
  other( SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_LOST ); key( SDLK_a );
  assert( read_events() == INPUT_REFRESH_SKIPPED );
  assert( !physical && cancellations == 1 && !ui_calls );
  normal_events();
  assert( !strcmp( order, "Cla" ) && cancellations == 1 && cancelled_events == 1 );
  key( SDLK_b );
  assert( read_events() == INPUT_REFRESH_SKIPPED ); /* Unfocused: do not consume. */
  other( SDL_WINDOWEVENT, SDL_WINDOWEVENT_FOCUS_GAINED );
  normal_events();
  assert( collector.focused && read_events() == INPUT_REFRESH_SUCCESS );

  reset(); reject = 1; key( SDLK_a );
  assert( read_events() == INPUT_REFRESH_SKIPPED && !physical );
  normal_events();
  assert( physical == 1 && !collector.has_pending ); /* Unapplied event exactly once. */

  reset(); recurse = 1; key( SDLK_a );
  assert( read_events() == INPUT_REFRESH_SUCCESS && physical == 1 );
  recurse = 1; key( SDLK_b ); normal_events();
  assert( physical == 2 && !collector.reading && !collector.normal_depth );

  reset();
  for( i = 0; i < 70; i++ ) key( SDLK_a );
  assert( read_events() == INPUT_REFRESH_SKIPPED && physical == 64 );
  assert( read_events() == INPUT_REFRESH_SUCCESS && physical == 70 );

  reset(); other( SDL_QUIT, 0 ); key( SDLK_a );
  assert( read_events() == INPUT_REFRESH_SKIPPED && !ui_calls && !physical );
  normal_events(); assert( !strcmp( order, "ma" ) );

  reset(); memset( &event, 0, sizeof( event ) );
  event.type = SDL_DROPFILE; event.drop.file = SDL_strdup( "ignored.tap" );
  /* Install the retained event directly: SDL2-on-SDL3 compatibility libraries
     do not reliably round-trip synthetic drop payloads through SDL_PushEvent. */
  collector.pending = event; collector.has_pending = 1;
  sdl2_events_end( &collector ); /* Frees retained owned payload on teardown. */
  sdl2_events_end( &collector );

  reset(); SDL_Quit();
  assert( read_events() == INPUT_REFRESH_FAILED );
  assert( !collector.reading );
  puts( "SDL2 ordered event tests passed" );
  return 0;
}
