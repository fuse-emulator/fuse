/* Private lifecycle checks, shared by library and canonical-source consumers.
   Each accepted attempt runs in a fresh process; no restart is assumed. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuse.h"
#include "fuse-internal.h"
#include "event.h"
#include "debugger/debugger.h"
#include "machine.h"
#include "memory.h"
#include "spectrum.h"
#include "z80/z80.h"
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
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_NOT_READY );
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

/* Exercise callbacks on the actual CPU/event path, not a second scheduler. */
static int frame_callback_action, frame_callback_fired;

static void
frame_callback( libspectrum_dword when, int type, void *data )
{
  int status = 73;
  (void)when; (void)type; (void)data;
  frame_callback_fired++;
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_BUSY );
  assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
  fuse_legacy_shutdown(); /* Active dispatch must retain the session. */
  assert( machine_current );
  if( frame_callback_action == 1 ) assert( !machine_reset( 0 ) );
  if( frame_callback_action == 2 ) assert( !fuse_emulation_pause() );
  if( frame_callback_action == 3 ) debugger_exit_emulator( NULL );
  if( frame_callback_action == 4 ) assert( !machine_select_id( "48" ) );
}

static void
check_frame_contract( int argc, char **argv, int shutdown_only )
{
  int type, status = 73;
  libspectrum_dword paused_tstates;
  libspectrum_word paused_pc;
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_NOT_READY );
  assert( fuse_legacy_prepare( argc, argv ) == FUSE_LEGACY_INIT_READY );
  /* No automation: this bounded fixture also runs in the plain build. */
#ifdef ENABLE_AUTOMATION
  assert( !automation_active() );
#endif
  type = event_register( frame_callback, "frame-contract" );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( spectrum_frame_dispatch_complete );
  assert( spectrum_get_frame_count() == 1 );
  if( shutdown_only ) {
    fuse_legacy_shutdown();
    check_closed();
    return;
  }

  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( spectrum_get_frame_count() == 2 );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  paused_tstates = tstates; paused_pc = z80.pc.w;
  assert( spectrum_frame_dispatch_complete );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_PAUSED );
  assert( !fuse_emulation_unpause() );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_PAUSED );
  assert( tstates == paused_tstates && z80.pc.w == paused_pc );
  assert( spectrum_frame_dispatch_complete );
  /* Commands remain possible while paused, without executing CPU time. */
  assert( !machine_reset( 0 ) );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_PAUSED );
  assert( tstates == paused_tstates );
  assert( spectrum_frame_dispatch_complete );
  assert( !fuse_emulation_unpause() );

  /* Reset after frame dispatch in the same due-event drain. The debugger
     frame count returns to zero, but this turn must still complete. */
  frame_callback_action = 1;
  event_add( machine_current->timings.tstates_per_frame, type );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( frame_callback_fired == 1 && !spectrum_get_frame_count() );
  assert( !machine_reset( 0 ) ); /* Reset between turns is ordinary too. */
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );

  /* A new turn clears the previous completion flag. A pause before the next
     frame returns early with it still clear; resume finishes that frame. */
  frame_callback_action = 2;
  assert( spectrum_frame_dispatch_complete );
  event_add( tstates + 1, type );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_PAUSED );
  assert( frame_callback_fired == 2 );
  assert( !spectrum_frame_dispatch_complete );
  assert( !fuse_emulation_unpause() );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( spectrum_frame_dispatch_complete );

  /* Z80 HALT is not a host pause; interrupt delivery remains in frame dispatch. */
  writebyte_internal( 0x8000, 0x76 );
  z80.pc.w = 0x8000; z80.iff1 = z80.iff2 = 0;
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( z80.halted );
  z80.iff1 = z80.iff2 = 1; z80.im = 1;
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( !z80.halted );

  /* Queue replacement before the next frame must not strand a frame turn. */
  frame_callback_action = 4;
  event_add( tstates + 1, type );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_COMPLETED );
  assert( frame_callback_fired == 3 );

  /* Debugger exit in the same dispatch as frame completion must win. */
  frame_callback_action = 3;
  event_add( machine_current->timings.tstates_per_frame, type );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_EXITING );
  assert( frame_callback_fired == 4 );
  paused_tstates = tstates;
  assert( !fuse_emulation_pause() );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_EXITING );
  assert( tstates == paused_tstates ); /* Exit wins over pause. */
  assert( !fuse_emulation_unpause() );
  assert( !fuse_legacy_execute( &status ) && !status );
  assert( fuse_legacy_run_frame() == FUSE_LEGACY_RUN_NOT_READY );
  fuse_legacy_shutdown();
  check_closed();
}

static void
check_frame_run( int argc, char **argv, int expected_status, int prefix_only )
{
#ifdef ENABLE_AUTOMATION
  int status = 73, turns = 0;
  fuse_legacy_run_result result;
  assert( fuse_legacy_prepare( argc, argv ) == FUSE_LEGACY_INIT_READY );
  assert( automation_active() );
  do {
    result = fuse_legacy_run_frame();
    turns++;
  } while( result == FUSE_LEGACY_RUN_COMPLETED &&
           ( !prefix_only || turns < 7 ) );
  assert( result == FUSE_LEGACY_RUN_EXITING ||
          ( prefix_only && turns == 7 && result == FUSE_LEGACY_RUN_COMPLETED ) );
  /* Only the compatibility runner finalizes application automation output. */
  assert( !fuse_legacy_execute( &status ) && status == expected_status );
  fuse_legacy_shutdown();
  check_closed();
#else
  (void)argc; (void)argv; (void)expected_status; (void)prefix_only;
  assert( 0 );
#endif
}

static void
check_preferences( const char *kind, char *rom )
{
  char program[] = "preference-test", machine[] = "--machine", model[] = "48";
  char rom_option[] = "--rom-48", autosave[] = "--autosave-settings";
  char no_save[] = "--no-autosave-settings", no_sound[] = "--no-sound";
  char frequency[] = "--sound-freq", original[] = "12345", changed[] = "23456";
  char *args[] = { program, machine, model, rom_option, rom, autosave,
                  frequency, original, no_sound, NULL };

  if( !strcmp( kind, "core-preferences" ) ||
      !strcmp( kind, "core-executed-preferences" ) ) {
    assert( fuse_legacy_prepare( 9, args ) == FUSE_LEGACY_INIT_READY );
    /* The user's setting is retained; it does not grant the core permission
       to persist settings. Mutate a value so an unwanted rewrite is observable. */
    assert( settings_current.autosave_settings );
    assert( settings_current.sound_freq == 12345 );
    settings_current.sound_freq = 23456;
    if( !strcmp( kind, "core-executed-preferences" ) ) {
      int status = 73;
      fuse_exiting = 1;
      assert( !fuse_legacy_execute( &status ) && status == 0 );
    }
    fuse_legacy_shutdown();
  } else {
    if( !strcmp( kind, "app-no-preferences" ) ) {
      args[5] = no_save;
      args[7] = changed;
    } else assert( !strcmp( kind, "app-preferences" ) );
    /* Exercise the actual compatibility wrapper without unbounded execution.
       The existing exit flag represents an exit requested during startup. */
    fuse_exiting = 1;
    assert( !fuse_legacy_main( 9, args ) );
  }
  check_closed();
}

int
legacy_lifecycle_test( int argc, char **argv )
{
  assert( argc >= 2 );
  libspectrum_error_function = preserved_error;
  if( !strcmp( argv[1], "frame-contract" ) ||
      !strcmp( argv[1], "frame-shutdown" ) ) {
    check_frame_contract( argc - 1, argv + 1,
                          !strcmp( argv[1], "frame-shutdown" ) );
  } else if( !strcmp( argv[1], "frames" ) ||
             !strcmp( argv[1], "frames-failure" ) ||
             !strcmp( argv[1], "frames-prefix" ) ) {
    check_frame_run( argc - 1, argv + 1,
                     !strcmp( argv[1], "frames-failure" ) ? 2 : 0,
                     !strcmp( argv[1], "frames-prefix" ) );
  } else if( !strcmp( argv[1], "core-preferences" ) ||
      !strcmp( argv[1], "core-executed-preferences" ) ||
      !strcmp( argv[1], "app-preferences" ) ||
      !strcmp( argv[1], "app-no-preferences" ) ) {
    assert( argc == 3 );
    check_preferences( argv[1], argv[2] );
  } else if( !strcmp( argv[1], "arguments" ) ) {
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
