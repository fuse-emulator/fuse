/* wdfdc_fakes.c: minimal Fuse environment for standalone WD FDC tests */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "libspectrum.h"

#include "event.h"
#include "infrastructure/startup_manager.h"
#include "machine.h"
#include "settings.h"
#include "ui/ui.h"


#define TEST_MAX_EVENT_TYPES 16
#define TEST_MAX_EVENTS      64


typedef struct test_event_type {
  event_fn_t fn;
  const char *description;
} test_event_type;


typedef struct test_event {
  int active;
  libspectrum_dword event_time;
  int type;
  void *user_data;
} test_event;


static test_event_type test_event_types[ TEST_MAX_EVENT_TYPES ];
static test_event test_events[ TEST_MAX_EVENTS ];
static int test_event_type_count = 1;


/*
 * Keep event type 0 reserved as the null event.
 */
int event_type_null = 0;
libspectrum_dword event_next_event = 0xffffffff;


/*
 * Global Fuse clock used by wd_fdc.c and fdd.c.
 */
libspectrum_dword tstates = 0;


/*
 * Minimal machine state.
 */
static fuse_machine_info test_machine;
fuse_machine_info *machine_current = &test_machine;


/*
 * fdd.c references this global.
 */
settings_info settings_current;


static const char *
test_event_name( int type )
{
  if( type <= 0 || type >= test_event_type_count )
    return "<invalid>";

  if( !test_event_types[ type ].description )
    return "<unnamed>";

  return test_event_types[ type ].description;
}


/*
 * Recalculate the timestamp of the earliest pending event.
 */
static void
test_update_next_event( void )
{
  int i;
  libspectrum_dword next = 0xffffffff;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {

    if( test_events[ i ].active &&
        test_events[ i ].event_time < next )
      next = test_events[ i ].event_time;
  }

  event_next_event = next;
}


int
event_register( event_fn_t fn, const char *description )
{
  int type;

  if( test_event_type_count >= TEST_MAX_EVENT_TYPES ) {
    fprintf( stderr, "Too many test event types\n" );
    fflush( stderr );
    return -1;
  }

  type = test_event_type_count++;

  test_event_types[ type ].fn = fn;
  test_event_types[ type ].description = description;

  return type;
}


void
event_add_with_data( libspectrum_dword event_time, int type,
                     void *user_data )
{
  int i;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {

    if( !test_events[ i ].active ) {

      test_events[ i ].active = 1;
      test_events[ i ].event_time = event_time;
      test_events[ i ].type = type;
      test_events[ i ].user_data = user_data;

      test_update_next_event();

      return;
    }
  }

  fprintf( stderr, "Test event queue full\n" );
  fflush( stderr );
}


void
event_remove_type( int type )
{
  int i;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {
    if( test_events[ i ].active && test_events[ i ].type == type )
      test_events[ i ].active = 0;
  }

  test_update_next_event();
}


void
event_remove_type_user_data( int type, gpointer user_data )
{
  int i;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {
    if( test_events[ i ].active &&
        test_events[ i ].type == type &&
        test_events[ i ].user_data == user_data )
      test_events[ i ].active = 0;
  }

  test_update_next_event();
}


/* Run the earliest scheduled event through the real Fuse callback. */
int
wdfdc_test_run_next_event( void )
{
  int i;
  int selected = -1;
  libspectrum_dword next = 0xffffffff;
  test_event event;
  event_fn_t fn;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {

    if( test_events[ i ].active &&
        test_events[ i ].event_time < next ) {

      next = test_events[ i ].event_time;
      selected = i;
    }
  }

  if( selected < 0 ) return 1;

  event = test_events[ selected ];
  test_events[ selected ].active = 0;

  test_update_next_event();

  tstates = event.event_time;

  if( event.type <= 0 ||
      event.type >= test_event_type_count ||
      !test_event_types[ event.type ].fn ) {

    fprintf(
      stderr,
      "Invalid test event type %d\n",
      event.type
      );
    fflush( stderr );

    return 1;
  }

  fn = test_event_types[ event.type ].fn;

  fn(
    tstates,
    event.type,
    event.user_data
    );

  return 0;
}


/*
 * Reset scheduler and virtual clock.
 *
 * Event registrations are preserved because fdd.c and wd_fdc.c keep their
 * event type IDs in static variables.
 */
void
wdfdc_test_reset_events( void )
{
  memset( test_events, 0, sizeof( test_events ) );

  tstates = 0;
  event_next_event = 0xffffffff;

}


/*
 * Initialise the subset of machine state used by FDD/WD FDC.
 */
void
wdfdc_test_machine_init( void )
{
  memset( &test_machine, 0, sizeof( test_machine ) );
  memset( &settings_current, 0, sizeof( settings_current ) );

  test_machine.machine = LIBSPECTRUM_MACHINE_48;

  test_machine.timings.processor_speed =
    libspectrum_timings_processor_speed(
      test_machine.machine
      );

  settings_current.drive_40_max_track = 40;
  settings_current.drive_80_max_track = 80;

  wdfdc_test_reset_events();
}


/*
 * The standalone harness has no real startup manager. Execute the supplied
 * module init callback immediately.
 */
void
startup_manager_register(
  startup_manager_module module,
  startup_manager_module *dependencies,
  size_t dependency_count,
  startup_manager_init_fn init_fn,
  void *init_context,
  startup_manager_end_fn end_fn )
{
  int result = 0;

  (void)dependencies;
  (void)dependency_count;
  (void)end_fn;

  if( init_fn )
    result = init_fn( init_context );

  if( result )
    fprintf( stderr, "Test module %d initialisation failed: %d\n",
             module, result );
}


/*
 * UPD765 events are irrelevant to these tests.
 */
void
upd_fdc_init_events( void )
{
}


/*
 * No real UI exists in this executable.
 */
int
ui_error( ui_error_level severity, const char *format, ... )
{
  va_list ap;

  fprintf(
    stderr,
    "[UI ERROR severity=%d] ",
    severity
    );

  va_start( ap, format );
  vfprintf( stderr, format, ap );
  va_end( ap );

  fprintf( stderr, "\n" );
  fflush( stderr );

  return 0;
}


int
ui_statusbar_update(
  ui_statusbar_item item,
  ui_statusbar_state state
  )
{
  (void)item;
  (void)state;

  return 0;
}


int
wdfdc_test_event_count( const char *description )
{
  int i;
  int count = 0;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {
    if( test_events[ i ].active &&
        !strcmp( test_event_name( test_events[ i ].type ), description ) )
      count++;
  }

  return count;
}


libspectrum_dword
wdfdc_test_event_time( const char *description )
{
  int i;
  libspectrum_dword next = 0xffffffff;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {
    if( test_events[ i ].active &&
        !strcmp( test_event_name( test_events[ i ].type ), description ) &&
        test_events[ i ].event_time < next )
      next = test_events[ i ].event_time;
  }

  return next;
}


int
wdfdc_test_run_event( const char *description )
{
  int i;
  int selected = -1;
  libspectrum_dword next = 0xffffffff;
  test_event event;

  for( i = 0; i < TEST_MAX_EVENTS; i++ ) {
    if( test_events[ i ].active &&
        !strcmp( test_event_name( test_events[ i ].type ), description ) &&
        test_events[ i ].event_time < next ) {
      next = test_events[ i ].event_time;
      selected = i;
    }
  }

  if( selected < 0 )
    return 1;

  event = test_events[ selected ];
  test_events[ selected ].active = 0;
  test_update_next_event();
  tstates = event.event_time;

  if( event.type <= 0 || event.type >= test_event_type_count ||
      !test_event_types[ event.type ].fn )
    return 1;

  test_event_types[ event.type ].fn( tstates, event.type, event.user_data );
  return 0;
}
