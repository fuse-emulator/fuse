/* Private lifecycle checks, shared by library and canonical-source consumers.
   Each accepted attempt runs in a fresh process; no restart is assumed. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuse.h"
#include "fuse-internal.h"
#include "machine.h"
#include "settings.h"
#include "ui/ui.h"
#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif

static libspectrum_error
preserved_error( libspectrum_error error, const char *format, va_list ap )
{
  (void)format;
  (void)ap;
  return error;
}

static void
check_closed( void )
{
  int status = 73;
  assert( !machine_current && !machine_types && !machine_count );
  assert( !settings_current.rom_48 && !settings_current.sound_device );
  assert( libspectrum_error_function == preserved_error );
  assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_CLOSED );
  fuse_legacy_shutdown();
  fuse_legacy_shutdown();
  assert( libspectrum_error_function == preserved_error );
#ifdef ENABLE_AUTOMATION
  assert( !automation_active() );
#endif
}

static void
check_invalid_arguments( void )
{
  char program[] = "lifecycle-test", extra[] = "not-a-terminator";
  char *missing[] = { program, NULL };
  char *unterminated[] = { program, extra };
  int status = 73;
  const char *previous_program = fuse_progname;

  fuse_legacy_shutdown();
  fuse_legacy_shutdown();
  assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
  assert( fuse_legacy_prepare( -1, NULL ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  assert( fuse_legacy_prepare( 1, NULL ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  assert( fuse_legacy_prepare( 2, missing ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  assert( fuse_legacy_prepare( 1, unterminated ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  assert( fuse_legacy_prepare( 0, unterminated ) == FUSE_LEGACY_INIT_INVALID_ARGUMENT );
  assert( !machine_current && !settings_current.rom_48 );
  assert( libspectrum_error_function == preserved_error );
  assert( fuse_progname == previous_program );
  /* A valid help attempt below must still be accepted. */
}

static void
check_complete( const char *kind )
{
  char program[] = "lifecycle-test", autosave[] = "--autosave-settings";
  char help[] = "--help", version[] = "--version";
  char rom_option[] = "--rom-48", invalid[] = "invalid-lifecycle.rom";
  char *args[] = { program, autosave, help, NULL, NULL };
  int count = 3;
  fuse_legacy_init_result expected = FUSE_LEGACY_INIT_HELP;

  if( !strcmp( kind, "version" ) ) {
    args[2] = version;
    expected = FUSE_LEGACY_INIT_VERSION;
  } else if( !strcmp( kind, "both" ) ) {
    args[3] = version;
    count = 4;
    expected = FUSE_LEGACY_INIT_VERSION; /* Existing version-before-help policy. */
  } else if( !strcmp( kind, "failure" ) ) {
    FILE *file = fopen( invalid, "wb" );
    assert( file && fwrite( "bad", 1, 3, file ) == 3 );
    assert( !fclose( file ) );
    args[2] = rom_option;
    args[3] = invalid;
    count = 4;
    settings_default.rom_48 = invalid; /* Prevent installed fallback masking it. */
    expected = FUSE_LEGACY_INIT_FAILED;
  } else assert( !strcmp( kind, "help" ) );

  assert( fuse_legacy_prepare( count, args ) == expected );
  check_closed();
  if( expected == FUSE_LEGACY_INIT_FAILED ) assert( !remove( invalid ) );
}

static void
check_session( int argc, char **argv, int execute, int expected_status )
{
  int status = 73;
  const char *program;
  assert( fuse_legacy_prepare( argc, argv ) == FUSE_LEGACY_INIT_READY );
  assert( machine_current && machine_types && machine_count );
  assert( settings_current.rom_48 && !settings_current.autosave_settings );
  assert( libspectrum_error_function == ui_libspectrum_error );
  program = fuse_progname;
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
  assert( fuse_progname == program && machine_current );
  assert( libspectrum_error_function == ui_libspectrum_error );
  assert( fuse_legacy_execute( NULL ) == 1 ); /* Does not consume READY. */

  if( execute ) {
#ifdef ENABLE_AUTOMATION
    assert( automation_active() );
    assert( !fuse_legacy_execute( &status ) && status == expected_status );
    /* Completion retains resources until shutdown, but cannot run twice. */
    assert( machine_current && settings_current.rom_48 );
    assert( libspectrum_error_function == ui_libspectrum_error );
    status = 73;
    assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
    assert( fuse_legacy_prepare( argc, argv ) == FUSE_LEGACY_INIT_BUSY );
#else
    (void)expected_status;
    assert( 0 ); /* Unbounded execution is deliberately not a test mode. */
#endif
  }
  fuse_legacy_shutdown();
  check_closed();
}

int
legacy_lifecycle_test( int argc, char **argv )
{
  assert( argc >= 2 );
  libspectrum_error_function = preserved_error;
  if( !strcmp( argv[1], "arguments" ) ) {
    check_invalid_arguments();
    check_complete( "help" );
  } else if( !strcmp( argv[1], "zero" ) ) {
    assert( argc == 3 );
    settings_default.rom_48 = argv[2];
    settings_default.autosave_settings = 0;
    assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_READY );
    assert( machine_current );
    fuse_legacy_shutdown();
    check_closed();
  } else if( !strcmp( argv[1], "prepare" ) || !strcmp( argv[1], "run" ) ||
             !strcmp( argv[1], "run-failure" ) ) {
    check_session( argc - 1, argv + 1, strcmp( argv[1], "prepare" ) != 0,
                   !strcmp( argv[1], "run-failure" ) ? 2 : 0 );
  } else {
    check_complete( argv[1] );
  }
  return 0;
}
