/* Startup ownership tests; no emulator/UI initialization is required. */
#include "config.h"

/* Checks must execute in release builds as well. */
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "infrastructure/startup_manager.h"
#include "ui/ui.h"

static char trace[32];
static size_t trace_length;
static int failure, diagnostics, recursive_end, partial_owned;

static void
record( char value )
{
  assert( trace_length + 1 < sizeof( trace ) );
  trace[ trace_length++ ] = value;
  trace[ trace_length ] = '\0';
}

int
ui_error( ui_error_level severity, const char *format, ... )
{
  assert( severity == UI_ERROR_ERROR );
  assert( format != NULL );
  diagnostics++;
  return 0;
}

static void
reset( void )
{
  trace_length = 0;
  trace[0] = '\0';
  failure = diagnostics = recursive_end = partial_owned = 0;
}

static int
start( void *context )
{
  int id = *(int *)context;
  record( 'a' + id - 1 );

  /* Model an initializer which cleans its own partial resource on failure.
     Its end callback must not subsequently run. */
  partial_owned++;
  if( failure == id ) {
    partial_owned--;
    record( '!' );
    return 17;
  }
  partial_owned--;
  return 0;
}

static void
end_a( void )
{
  record( 'A' );
  if( recursive_end ) startup_manager_run_end();
}

static void
end_b( void )
{
  record( 'B' );
  if( recursive_end ) startup_manager_run_end();
}

static void
end_c( void )
{
  record( 'C' );
  if( recursive_end ) startup_manager_run_end();
}

static void
register_chain( void )
{
  static int ids[] = { 1, 2, 3 };
  startup_manager_module after_a[] = { STARTUP_MANAGER_MODULE_AY };
  startup_manager_module after_b[] = { STARTUP_MANAGER_MODULE_BETA };

  /* Deliberately register in a different order from the dependency order. */
  startup_manager_register( STARTUP_MANAGER_MODULE_COVOX, after_b, 1,
                            start, &ids[2], end_c );
  startup_manager_register_no_dependencies( STARTUP_MANAGER_MODULE_AY,
                                            start, &ids[0], end_a );
  startup_manager_register( STARTUP_MANAGER_MODULE_BETA, after_a, 1,
                            start, &ids[1], end_b );
}

static void
test_success( int recurse )
{
  reset();
  recursive_end = recurse;
  startup_manager_init();
  register_chain();
  assert( startup_manager_run() == 0 );
  assert( !strcmp( trace, "abc" ) );
  startup_manager_run_end();
  assert( !strcmp( trace, "abcCBA" ) );
  startup_manager_run_end();
  assert( !strcmp( trace, "abcCBA" ) );
  assert( !diagnostics && !partial_owned );
}

static void
test_failure( void )
{
  static const char *expected[] = { "a!", "ab!A", "abc!BA" };
  int i;

  for( i = 1; i <= 3; i++ ) {
    reset();
    failure = i;
    recursive_end = 1;
    startup_manager_init();
    register_chain();
    assert( startup_manager_run() == 17 );
    startup_manager_run_end();
    assert( !strcmp( trace, expected[i - 1] ) );
    startup_manager_run_end();
    assert( !strcmp( trace, expected[i - 1] ) );
    assert( !diagnostics && !partial_owned );
  }
}

static void
test_pending_registrations( void )
{
  startup_manager_module after_b[] = { STARTUP_MANAGER_MODULE_BETA };
  startup_manager_module after_c[] = { STARTUP_MANAGER_MODULE_COVOX };
  static int id = 1;

  reset();
  startup_manager_init();
  startup_manager_register_no_dependencies( STARTUP_MANAGER_MODULE_AY,
                                            start, &id, end_a );
  startup_manager_register( STARTUP_MANAGER_MODULE_BETA, after_c, 1,
                            NULL, NULL, end_b );
  startup_manager_register( STARTUP_MANAGER_MODULE_COVOX, after_b, 1,
                            NULL, NULL, end_c );
  assert( startup_manager_run() != 0 );
  assert( diagnostics == 1 );
  startup_manager_run_end();
  assert( !strcmp( trace, "aA" ) );
  startup_manager_run_end();

  /* Abandoning registrations before run must not invoke their end callbacks. */
  reset();
  startup_manager_init();
  register_chain();
  startup_manager_run_end();
  startup_manager_run_end();
  assert( !trace_length && !diagnostics );
}

static void
test_end_only_registration( void )
{
  reset();
  startup_manager_init();
  startup_manager_register_no_dependencies( STARTUP_MANAGER_MODULE_AY,
                                            NULL, NULL, end_a );
  assert( startup_manager_run() == 0 );
  startup_manager_run_end();
  startup_manager_run_end();
  assert( !strcmp( trace, "A" ) );
}

int
main( void )
{
  startup_manager_run_end();
  startup_manager_run_end();
  startup_manager_init();
  assert( startup_manager_run() == 0 );
  startup_manager_run_end();
  test_success( 0 );
  test_success( 1 );
  test_failure();
  test_pending_registrations();
  test_end_only_registration();
  puts( "startup manager tests passed" );
  return 0;
}
