/* fuse.c: The Free Unix Spectrum Emulator
   Copyright (c) 1999-2026 Philip Kendall and others

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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <unistd.h>

#ifdef HAVE_LIB_XML2
#include <libxml/parser.h>
#endif

#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif
#include "debugger/debugger.h"
#include "display.h"
#include "event.h"
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-engine.h"
#include "fuse-legacy.h"
#include "infrastructure/startup_manager.h"
#include "infrastructure/startup_files.h"
#include "infrastructure/startup_test.h"
#include "keyboard.h"
#include "keyboard_refresh.h"
#include "machine.h"
#include "machines/machines_periph.h"
#include "memory_pages.h"
#include "module.h"
#include "movie.h"
#include "mempool.h"
#include "peripherals/dck.h"
#include "peripherals/sound/ay.h"
#include "peripherals/sound/fuller.h"
#include "peripherals/sound/melodik.h"
#include "peripherals/disk/beta.h"
#include "peripherals/disk/didaktik.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/ide/divide.h"
#include "peripherals/ide/divmmc.h"
#include "peripherals/ide/simpleide.h"
#include "peripherals/ide/zxatasp.h"
#include "peripherals/ide/zxcf.h"
#include "peripherals/ide/zxmmc.h"
#include "peripherals/joystick.h"
#include "peripherals/if1.h"
#include "peripherals/if2.h"
#include "peripherals/kempmouse.h"
#include "peripherals/multiface.h"
#include "peripherals/printer.h"
#include "peripherals/scld.h"
#include "peripherals/sound/uspeech.h"
#include "peripherals/speccyboot.h"
#include "peripherals/spectranet.h"
#include "peripherals/ttx2000s.h"
#include "peripherals/ula.h"
#include "peripherals/usource.h"
#include "phantom_typist.h"
#include "pokefinder/pokemem.h"
#include "profile.h"
#include "psg.h"
#include "rzx.h"
#include "screenshot.h"
#include "settings.h"
#include "slt.h"
#include "snapshot.h"
#include "sound.h"
#include "spectrum.h"
#include "tape.h"
#include "timer/timer.h"
#include "ui/scaler/scaler.h"
#include "ui/ui.h"
#include "ui/uimedia.h"
#ifdef UI_NULL
#include "unittests/unittests.h"
#endif
#include "utils.h"

#include "z80/z80.h"

/* What name were we called under? */
const char *fuse_progname;

/* A flag to say when we want to exit the emulator */
int fuse_exiting;

/* Is Spectrum emulation currently paused, and if so, how many times? */
int fuse_emulation_paused;

/* The creator information we'll store in file formats that support this */
libspectrum_creator *fuse_creator;

/* The earliest version of libspectrum we need */
static const char * const LIBSPECTRUM_MIN_VERSION = "0.5.0";

/* The selected UI may rewrite the application's argument list. */
typedef struct {
  int *argc;
  char ***argv;
} display_startup_context;

typedef enum legacy_phase {
  LEGACY_FRESH, LEGACY_STARTING, LEGACY_READY, LEGACY_RUNNING,
  LEGACY_FINISHED, LEGACY_OPERATING, LEGACY_FAILED, LEGACY_STOPPING, LEGACY_CLOSED
} legacy_phase;

/* Configured legacy-engine ownership, not independent emulation contexts. */
static struct {
  legacy_phase phase;
  int manager_started;
  int ui_owned;
  int save_settings;
  int execution_started;
  int xml_owned;
  int libspectrum_owned;
  int callback_owned;
  int resource_failed;
  libspectrum_error_function_t previous_error_function;
} legacy_ownership;

/* Only the current budget turn/dispatch, not a persistent machine clock. */
static struct {
  int active, interrupted;
  libspectrum_dword deadline;
  int64_t rebased;
} legacy_budget;

void
fuse_budget_rebase( unsigned long before, unsigned long after )
{
  if( legacy_budget.active )
    legacy_budget.rebased += (int64_t)before - (int64_t)after;
}

void
fuse_budget_interrupt( void )
{
  if( !legacy_budget.active ) return;
  legacy_budget.interrupted = 1;
  legacy_budget.deadline = 0; /* End an active CPU batch after clock edits. */
}

static void fuse_settings_end( void );
static int fuse_init( int argc, char **argv, const fuse_host_config *config );

static void creator_register_startup( void );

static void fuse_show_copyright(void);
static void fuse_show_version( void );
static void fuse_show_test_build_info( void );
static void fuse_show_help( void );

static int parse_nonoption_args( int argc, char **argv, int first_arg,
                                 fuse_start_files *start_files );
static int do_start_files( fuse_start_files *start_files );

static int fuse_end(void);

fuse_init_result
fuse_legacy_prepare( int argc, char **argv )
{
  int i;
  fuse_init_result result;

  if( legacy_ownership.phase == LEGACY_CLOSED ) return FUSE_INIT_CLOSED;
  if( legacy_ownership.phase != LEGACY_FRESH ) return FUSE_INIT_BUSY;
  if( argc < 0 || ( argc && !argv ) ) return FUSE_INIT_INVALID_ARGUMENT;
  if( argv ) {
    for( i = 0; i < argc; i++ )
      if( !argv[i] ) return FUSE_INIT_INVALID_ARGUMENT;
    if( argv[argc] ) return FUSE_INIT_INVALID_ARGUMENT;
  }

  /* Guard before any library/frontend callback. Preserve the existing RNG,
     platform startup and library initialization ordering inside fuse_init. */
  legacy_ownership.phase = LEGACY_STARTING;
  legacy_ownership.previous_error_function = libspectrum_error_function;
  legacy_ownership.callback_owned = 1;

  if(fuse_init(argc,argv,NULL)) {
    fprintf(stderr,"%s: error initialising -- giving up!\n", fuse_progname);
    fuse_end();
    return FUSE_INIT_FAILED;
  }

  if( settings_current.show_help || settings_current.show_version ) {
    result = settings_current.show_version ? FUSE_INIT_VERSION :
                                            FUSE_INIT_HELP;
    fuse_end();
    return result;
  }

  legacy_ownership.phase = LEGACY_READY;
  return FUSE_INIT_READY;
}

fuse_init_result
fuse_host_prepare( const fuse_host_config *config,
                   const fuse_host_resources *resources )
{
  if( legacy_ownership.phase == LEGACY_CLOSED ) return FUSE_INIT_CLOSED;
  if( legacy_ownership.phase != LEGACY_FRESH ) return FUSE_INIT_BUSY;
  if( !config || !resources || !resources->acquire_rom )
    return FUSE_INIT_INVALID_ARGUMENT;

  legacy_ownership.phase = LEGACY_STARTING;
  legacy_ownership.previous_error_function = libspectrum_error_function;
  legacy_ownership.callback_owned = 1;
  fuse_host_begin_resources( resources );

  if( fuse_init( 0, NULL, config ) || legacy_ownership.resource_failed ) {
    fuse_end();
    return FUSE_INIT_FAILED;
  }
  legacy_ownership.phase = LEGACY_READY;
  return FUSE_INIT_READY;
}

void
fuse_shutdown( void )
{
  if( legacy_ownership.phase == LEGACY_READY ||
      legacy_ownership.phase == LEGACY_FINISHED ||
      legacy_ownership.phase == LEGACY_FAILED ) fuse_end();
}

fuse_host_operation_result
fuse_engine_check_operation( void )
{
  if( legacy_ownership.phase == LEGACY_STARTING ||
      legacy_ownership.phase == LEGACY_RUNNING ||
      legacy_ownership.phase == LEGACY_OPERATING ||
      legacy_ownership.phase == LEGACY_STOPPING ) return FUSE_HOST_OPERATION_BUSY;
  if( legacy_ownership.phase != LEGACY_READY ) return FUSE_HOST_OPERATION_NOT_READY;
  if( fuse_exiting ) return FUSE_HOST_OPERATION_EXITING;
  if( fuse_host_quit_pending() ) return FUSE_HOST_OPERATION_BUSY;
  return FUSE_HOST_OPERATION_OK;
}

/* Some legacy peripheral reset callbacks disable themselves on ROM failure.
   Keep their behaviour, but do not publish false success to a host mutation. */
void
fuse_host_resource_error( void )
{
  if( legacy_ownership.phase == LEGACY_OPERATING ||
      ( legacy_ownership.phase == LEGACY_STARTING && fuse_host_resources_active() ) )
    legacy_ownership.resource_failed = 1;
}

void
fuse_engine_begin_operation( void )
{
  legacy_ownership.resource_failed = 0;
  legacy_ownership.phase = LEGACY_OPERATING;
}

fuse_host_operation_result
fuse_engine_end_machine_operation( int error )
{
  if( legacy_ownership.resource_failed ) error = 1;
  legacy_ownership.phase = error ? LEGACY_FAILED : LEGACY_READY;
  return error ? FUSE_HOST_OPERATION_FAILED : FUSE_HOST_OPERATION_OK;
}

void
fuse_engine_end_operation( void )
{
  legacy_ownership.phase = LEGACY_READY;
}

int
fuse_legacy_main( int argc, char **argv )
{
  int r = 1;
  switch( fuse_legacy_prepare( argc, argv ) ) {
  case FUSE_INIT_READY:
    /* Automatic preference persistence belongs to the application wrapper,
       not to core preparation or ordinary resource disposal. */
    legacy_ownership.save_settings = 1;
    if( fuse_legacy_execute( &r ) ) r = 1;
    fuse_shutdown();
    return r;
  case FUSE_INIT_HELP:
  case FUSE_INIT_VERSION:
    return 0;
  case FUSE_INIT_BUSY:
  case FUSE_INIT_CLOSED:
    fprintf( stderr, "Fuse legacy runner supports only one session attempt\n" );
    return 1;
  case FUSE_INIT_INVALID_ARGUMENT:
    fprintf( stderr, "Invalid Fuse legacy startup arguments\n" );
    return 1;
  case FUSE_INIT_FAILED:
    return 1;
  }
  return 1;
}

/* Only the continuous runner opts in, for the duration of a dispatch. */
static int pump_frame_ui;

int
fuse_engine_pumps_frame_ui( void )
{
  return pump_frame_ui;
}

/* The owner thread owns a single pending decision and process-wide IDs. */
static fuse_decision_owner decision_owner;
static fuse_media_decision_id decision_sequence;

fuse_decision_owner
fuse_engine_pending_decision( void )
{
  return decision_owner;
}

fuse_media_decision_id
fuse_engine_request_decision( fuse_decision_owner owner )
{
  if( decision_owner != FUSE_DECISION_NONE )
    return decision_owner == owner ? decision_sequence : 0;
  if( ++decision_sequence == 0 ) ++decision_sequence;
  decision_owner = owner;
  return decision_sequence;
}

fuse_host_operation_result
fuse_engine_validate_decision( fuse_decision_owner owner, fuse_media_decision_id id,
                               fuse_media_decision_choice choice, const char *path,
                               int save_requires_path )
{
  if( !id || id != decision_sequence || decision_owner != owner )
    return FUSE_HOST_OPERATION_STALE;
  switch( choice ) {
  case FUSE_MEDIA_DECISION_SAVE:
    if( path ? !*path : save_requires_path ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
    break;
  case FUSE_MEDIA_DECISION_CANCEL:
  case FUSE_MEDIA_DECISION_DISCARD:
    if( path ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
    break;
  default:
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  }
  return FUSE_HOST_OPERATION_OK;
}

void
fuse_engine_retire_decision( fuse_decision_owner owner )
{
  if( decision_owner == owner ) decision_owner = FUSE_DECISION_NONE;
}

static void
fuse_check_execution_limits( void )
{
#ifdef ENABLE_AUTOMATION
  if( automation_active() &&
      automation_frame_limit_reached( spectrum_get_frame_count() ) )
    fuse_exiting = 1;
#endif
}

static void
fuse_dispatch_legacy_batch( void )
{
  pump_frame_ui = 1;
  z80_do_opcodes();
  event_do_events();
  pump_frame_ui = 0;
  fuse_check_execution_limits();
}

static void
fuse_dispatch_host_batch( const libspectrum_dword *deadline )
{
  debugger_host_run_batch( deadline );
  /* Return at the breakpoint without dispatching any more emulated work. */
  if( debugger_host_stopped() ) return;
  event_do_events();
  fuse_check_execution_limits();
}

/* Host turns and the legacy runner share one automation session. Resuming
   through the legacy runner must not re-arm its deadline after host turns. */
static void
fuse_execution_start( void )
{
  if( legacy_ownership.execution_started ) return;
  legacy_ownership.execution_started = 1;
#ifdef ENABLE_AUTOMATION
  if( automation_active() ) automation_arm( spectrum_get_frame_count() );
#endif
}

fuse_run_result
fuse_run_frame( void )
{
  fuse_run_result result;

  if( legacy_ownership.phase == LEGACY_STARTING ||
      legacy_ownership.phase == LEGACY_OPERATING ||
      legacy_ownership.phase == LEGACY_RUNNING ||
      legacy_ownership.phase == LEGACY_STOPPING ) return FUSE_RUN_BUSY;
  if( legacy_ownership.phase != LEGACY_READY ) return FUSE_RUN_NOT_READY;
  if( fuse_exiting ) return FUSE_RUN_EXITING;
  if( decision_owner != FUSE_DECISION_NONE ) return FUSE_RUN_NEEDS_DECISION;
  if( debugger_host_stopped() ) return FUSE_RUN_DEBUGGER_STOPPED;
  if( fuse_emulation_paused ) return FUSE_RUN_PAUSED;

  legacy_ownership.phase = LEGACY_RUNNING;
  fuse_execution_start();
  spectrum_frame_dispatch_complete = 0;
  do {
    fuse_dispatch_host_batch( NULL );
  } while( !fuse_exiting && !debugger_host_stopped() && !fuse_emulation_paused &&
           !spectrum_frame_dispatch_complete );

  result = fuse_exiting ? FUSE_RUN_EXITING :
           debugger_host_stopped() ? FUSE_RUN_DEBUGGER_STOPPED :
           fuse_emulation_paused ? FUSE_RUN_PAUSED :
                                   FUSE_RUN_COMPLETED;
  legacy_ownership.phase = LEGACY_READY;
  return result;
}

fuse_run_result
fuse_run_tstates( unsigned long budget )
{
  unsigned long remaining = budget;
  fuse_run_result result;

  if( legacy_ownership.phase == LEGACY_OPERATING ||
      legacy_ownership.phase == LEGACY_STARTING ||
      legacy_ownership.phase == LEGACY_RUNNING ||
      legacy_ownership.phase == LEGACY_STOPPING ) return FUSE_RUN_BUSY;
  if( legacy_ownership.phase != LEGACY_READY ) return FUSE_RUN_NOT_READY;
  if( !budget || budget > 0x7fffffffUL || tstates >= 0x7fffffffUL )
    return FUSE_RUN_INVALID_ARGUMENT;
  if( fuse_exiting ) return FUSE_RUN_EXITING;
  if( decision_owner != FUSE_DECISION_NONE ) return FUSE_RUN_NEEDS_DECISION;
  if( debugger_host_stopped() ) return FUSE_RUN_DEBUGGER_STOPPED;
  if( fuse_emulation_paused ) return FUSE_RUN_PAUSED;

  legacy_ownership.phase = LEGACY_RUNNING;
  fuse_execution_start();
  legacy_budget.active = 1;
  legacy_budget.interrupted = 0;
  do {
    libspectrum_dword before = tstates;
    int64_t elapsed;

    /* Bound addition and leave headroom for instruction completion. Normal
       frame/RZX coordinates stay far below this cap. */
    if( tstates >= 0x7fffffffUL ) {
      legacy_budget.interrupted = 1;
      break;
    }
    legacy_budget.deadline = tstates +
      ( remaining < 0x7fffffffUL - tstates ? remaining :
                                            0x7fffffffUL - tstates );
    legacy_budget.rebased = 0;
    fuse_dispatch_host_batch( &legacy_budget.deadline );
    if( legacy_budget.interrupted || debugger_host_stopped() ) break;
    elapsed = (int64_t)tstates - before + legacy_budget.rebased;
    if( elapsed < 0 ) {
      legacy_budget.interrupted = 1;
      break;
    }
    if( (uint64_t)elapsed >= remaining ) {
      remaining = 0;
    } else {
      remaining -= (unsigned long)elapsed;
    }
  } while( remaining && !fuse_exiting && !fuse_emulation_paused );

  result = fuse_exiting ? FUSE_RUN_EXITING :
           debugger_host_stopped() ? FUSE_RUN_DEBUGGER_STOPPED :
           legacy_budget.interrupted ? FUSE_RUN_INTERRUPTED :
           fuse_emulation_paused ? FUSE_RUN_PAUSED :
                                   FUSE_RUN_COMPLETED;
  legacy_budget.active = 0;
  legacy_ownership.phase = LEGACY_READY;
  return result;
}

int
fuse_legacy_execute( int *exit_status )
{
  int r;
  if( legacy_ownership.phase != LEGACY_READY || !exit_status ||
      decision_owner != FUSE_DECISION_NONE || debugger_host_pending() ) return 1;
  legacy_ownership.phase = LEGACY_RUNNING;
#ifdef FUSE_STARTUP_TEST
  fuse_startup_test_running();
#endif

#ifdef UI_NULL
  if( settings_current.unittests ) {
    r = unittests_run();
  } else
#endif
  {
    fuse_execution_start();
    while( !fuse_exiting ) {
      fuse_dispatch_legacy_batch();
    }
    r = debugger_get_exit_code();
#ifdef ENABLE_AUTOMATION
    if( automation_active() ) r = automation_exit_status();
#endif
  }

#ifdef ENABLE_AUTOMATION
  if( automation_active() && automation_write_result() ) r = 1;
#endif
  legacy_ownership.phase = LEGACY_FINISHED;
  *exit_status = r;
  return 0;
}

static int
fuse_libspectrum_init( void *context )
{
  if( libspectrum_check_version( LIBSPECTRUM_MIN_VERSION ) ) {
    if( libspectrum_init() ) return 1;
    legacy_ownership.libspectrum_owned = 1;
  } else {
    ui_error( UI_ERROR_ERROR,
              "libspectrum version %s found, but %s required",
	      libspectrum_version(), LIBSPECTRUM_MIN_VERSION );
    return 1;
  }

  return 0;
}

static void
libspectrum_register_startup( void )
{
  startup_manager_module dependencies[] = {
    STARTUP_MANAGER_MODULE_DISPLAY
  };
  startup_manager_register( STARTUP_MANAGER_MODULE_LIBSPECTRUM, dependencies,
                            ARRAY_SIZE( dependencies ), fuse_libspectrum_init,
                            NULL, NULL );
}

/* Keep settings at their existing teardown position. Process-global XML
   cleanup runs only after all owned XML users (including SVG) have ended. */
static void
fuse_settings_end( void )
{
  settings_end( legacy_ownership.save_settings );
}

static int
libxml2_init( void *context )
{
  legacy_ownership.xml_owned = 1;
#ifdef HAVE_LIB_XML2
  LIBXML_TEST_VERSION
#endif

  return 0;
}

static void
libxml2_register_startup( void )
{
  startup_manager_module dependencies[] = { STARTUP_MANAGER_MODULE_SETUID };
  startup_manager_register( STARTUP_MANAGER_MODULE_LIBXML2, dependencies,
                            ARRAY_SIZE( dependencies ), libxml2_init, NULL,
                            NULL );
}

static int
setuid_init( void *context )
{
#ifdef HAVE_GETEUID
  int error;

  /* Privilege policy belongs to the application, never an embedding host. */
  if( fuse_host_resources_active() ) return 0;

  /* Drop root privs if we have them */
  if( !geteuid() ) {
    error = setuid( getuid() );
    if( error ) {
      ui_error( UI_ERROR_ERROR, "Could not drop root privileges" );
      return 1;
    }
  }
#endif				/* #ifdef HAVE_GETEUID */

  return 0;
}

static void
setuid_register_startup( void )
{
  startup_manager_module dependencies[] = {
    STARTUP_MANAGER_MODULE_DISPLAY,
    STARTUP_MANAGER_MODULE_LIBSPECTRUM,
  };
  startup_manager_register( STARTUP_MANAGER_MODULE_SETUID, dependencies,
                            ARRAY_SIZE( dependencies ), setuid_init, NULL,
                            NULL );
}

static void
fuse_ui_end( void )
{
  if( legacy_ownership.ui_owned ) {
    legacy_ownership.ui_owned = 0;
    ui_end();
  }
  display_ui_initialised = 0;
}

/* Keep desktop startup order without giving the renderer UI ownership. */
static int
fuse_display_startup( void *context )
{
  display_startup_context *ui = context;

  /* A host supplies a ready frontend and does not transfer its ownership. */
  if( ui ) {
    if( FUSE_STARTUP_FAIL( "display.ui" ) || ui_init( ui->argc, ui->argv ) )
      return 1;
    legacy_ownership.ui_owned = 1;
  }

  if( display_init() || FUSE_STARTUP_FAIL( "display.ready" ) ) {
    fuse_ui_end();
    display_end();
    return 1;
  }

  return 0;
}

static int
run_startup_manager( display_startup_context *ui GCC_UNUSED )
{
  startup_manager_init();
  legacy_ownership.manager_started = 1;

  /* Get every module to register its init function */
  ay_register_startup();
  beta_register_startup();
  creator_register_startup();
  covox_register_startup();
  debugger_register_startup();
  didaktik80_register_startup();
  disciple_register_startup();
#ifndef GEKKO
  startup_manager_register_no_dependencies( STARTUP_MANAGER_MODULE_DISPLAY,
                                            fuse_display_startup, ui, NULL );
#endif
  divide_register_startup();
  divmmc_register_startup();
  event_register_startup();
  fdd_register_startup();
  fuller_register_startup();
  if1_register_startup();
  if2_register_startup();
  joystick_register_startup();
  kempmouse_register_startup();
  keyboard_register_startup();
  libspectrum_register_startup();
  libxml2_register_startup();
  machine_register_startup();
  machines_periph_register_startup();
  melodik_register_startup();
  memory_register_startup();
  mempool_register_startup();
  multiface_register_startup();
  opus_register_startup();
  phantom_typist_register_startup();
  plusd_register_startup();
  printer_register_startup();
  profile_register_startup();
  psg_register_startup();
  rzx_register_startup();
  scld_register_startup();
  screenshot_register_startup();
  settings_register_startup( fuse_settings_end );
  setuid_register_startup();
  simpleide_register_startup();
  slt_register_startup();
  sound_register_startup();
  speccyboot_register_startup();
  specdrum_register_startup();
  spectranet_register_startup();
  spectrum_register_startup();
  tape_register_startup();
  ttx2000s_register_startup();
  timer_register_startup();
  ula_register_startup();
  usource_register_startup();
  uspeech_register_startup();
  z80_register_startup();
  zxatasp_register_startup();
  zxcf_register_startup();
  zxmmc_register_startup();

  return startup_manager_run();
}

static int
fuse_init( int argc, char **argv, const fuse_host_config *config )
{
  int error, first_arg = 0;
  display_startup_context desktop = { &argc, &argv };
  display_startup_context *ui = config ? NULL : &desktop;
  char *start_scaler;
  fuse_start_files start_files = { 0 };

  /* Seed the bad but widely-available random number
     generator with the current time */
  srand( (unsigned)time( NULL ) );

  /* Some platforms (e.g. Wii) do not have argc/argv */
  if(argc > 0)
    fuse_progname = argv[0];
  else
    fuse_progname = "fuse";
  
  libspectrum_error_function = ui_libspectrum_error;

#ifdef GEKKO
  /* On the Wii, init the display first so we have a way of outputting
     messages */
  if( fuse_display_startup( ui ) ) return 1;
#endif

  /* Config parsing can initialize XML before its manager registration runs. */
  if( config ) {
    settings_init_defaults();
    fuse_engine_apply_config( config );
  } else {
    legacy_ownership.xml_owned = 1;
    if( settings_init( &first_arg, argc, argv ) ) return 1;
  }
#ifndef UI_NULL
  if( settings_current.unittests ) {
    fprintf( stderr, "--unittests is available only with the null UI\n" );
    return 1;
  }
#endif
#ifdef ENABLE_AUTOMATION
  if( automation_active() ) settings_current.autosave_settings = 0;
#endif

  if( settings_current.show_version ) {
    fuse_show_version();
    return 0;
  } else if( settings_current.show_help ) {
    fuse_show_help();
    return 0;
  }

  start_scaler = utils_safe_strdup( settings_current.start_scaler_mode );

  if( !config ) fuse_show_copyright();

  error = run_startup_manager( ui );
  if( error ) goto cleanup;

  error = machine_select_id( settings_current.start_machine );
  if( error ) goto cleanup;
  if( FUSE_STARTUP_FAIL( "machine.selected" ) ) {
    error = 1;
    goto cleanup;
  }

  error = scaler_select_id( start_scaler );
  if( error ) goto cleanup;

  fuse_start_files_init( &start_files );
  error = config ? 0 : parse_nonoption_args( argc, argv, first_arg, &start_files );
  if( !error ) error = do_start_files( &start_files );
  fuse_start_files_end( &start_files );
  if( error ) goto cleanup;
  if( FUSE_STARTUP_FAIL( "startup.files" ) ) {
    error = 1;
    goto cleanup;
  }

  /* Must do this after all subsytems are initialised */
  if( !config ) debugger_command_evaluate( settings_current.debugger_command );

  if( ui_mouse_present ) ui_mouse_grabbed = ui_mouse_grab( 1 );

  fuse_emulation_paused = 0;
  movie_init();

#ifdef FUSE_TEST_BUILD
  /* Show warning in case of test build */
  ui_error( UI_ERROR_WARNING,
    FUSE_TEST_LINE "\n"
    FUSE_TEST_BUILD
  );
#endif

  error = 0;

cleanup:
  fuse_start_files_end( &start_files );
  libspectrum_free( start_scaler );
  return error;
}

static int
creator_init( void *context )
{
  size_t i;
  unsigned int version[4] = { 0, 0, 0, 0 };
  char *custom, osname[ 192 ];
  static const size_t CUSTOM_SIZE = 256;
  
  libspectrum_error error; int sys_error;

  const char *gcrypt_version;

  sscanf( VERSION, "%u.%u.%u.%u",
	  &version[0], &version[1], &version[2], &version[3] );

  for( i=0; i<4; i++ ) if( version[i] > 0xff ) version[i] = 0xff;

  sys_error = compat_osname( osname, sizeof( osname ) );
  if( sys_error ) return 1;

  fuse_creator = libspectrum_creator_alloc();

  error = libspectrum_creator_set_program( fuse_creator, "Fuse" );
  if( error ) { libspectrum_creator_free( fuse_creator ); return error; }

  error = libspectrum_creator_set_major( fuse_creator,
					 version[0] * 0x100 + version[1] );
  if( error ) { libspectrum_creator_free( fuse_creator ); return error; }

  error = libspectrum_creator_set_minor( fuse_creator,
					 version[2] * 0x100 + version[3] );
  if( error ) { libspectrum_creator_free( fuse_creator ); return error; }

  custom = libspectrum_new( char, CUSTOM_SIZE );

  gcrypt_version = libspectrum_gcrypt_version();
  if( !gcrypt_version ) gcrypt_version = "not available";

  snprintf( custom, CUSTOM_SIZE,
	    "gcrypt: %s\nlibspectrum: %s\nuname: %s", gcrypt_version,
	    libspectrum_version(), osname );

  error = libspectrum_creator_set_custom(
    fuse_creator, (libspectrum_byte*)custom, strlen( custom )
  );
  if( error ) {
    libspectrum_free( custom ); libspectrum_creator_free( fuse_creator );
    return error;
  }

  return 0;
}

static void
creator_end( void )
{
  libspectrum_creator_free( fuse_creator );
}

static void
creator_register_startup( void )
{
  startup_manager_module dependencies[] = { STARTUP_MANAGER_MODULE_SETUID };
  startup_manager_register( STARTUP_MANAGER_MODULE_CREATOR, dependencies,
                            ARRAY_SIZE( dependencies ), creator_init, NULL,
                            creator_end );
}

static void fuse_show_copyright(void)
{
  printf( "\n" );
  fuse_show_version();
  printf(
   FUSE_COPYRIGHT "; see the file\n"
   "'AUTHORS' for more details.\n"
   "\n"
   "For help, please mail <fuse-emulator-devel@lists.sf.net> or use\n"
   "the forums at <https://sourceforge.net/p/fuse-emulator/discussion/>.\n"
   "\n"
   "This program is distributed in the hope that it will be useful,\n"
   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
   "GNU General Public License for more details.\n\n");
}

static void fuse_show_version( void )
{
  printf( "The Free Unix Spectrum Emulator (Fuse) version " VERSION ".\n" );

  fuse_show_test_build_info();
}

static void fuse_show_test_build_info( void )
{
#ifdef FUSE_TEST_BUILD
  printf(
    "\n"
    FUSE_TEST_LINE "\n"
    FUSE_TEST_BUILD "\n"
    "\n"
    );
#endif
}

static void fuse_show_help( void )
{
  printf( "\n" );
  fuse_show_version();
#ifdef ENABLE_AUTOMATION
  printf(
   "\nDevelopment automation options:\n\n"
   "--automation-output <directory>       Write one-shot result artifacts here.\n"
   "--automation-frames <count>           Stop after completed machine frames.\n"
   "--automation-sample-tstates <n>       Sample PC n T-states after the final frame (probe only).\n"
   "--automation-max-frames <count>       Deadline for a bounded run.\n"
   "--automation-until-rzx-end            Stop when RZX playback ends.\n"
   "--automation-success-pc <address>     Stop successfully at this PC.\n"
   "--automation-failure-pc <address>     Stop unsuccessfully at this PC.\n"
   "--automation-failure-pc-ignore <n>    Ignore the first n failure hits.\n"
   "--automation-capture-screen           Write the final screen as PNG.\n"
   "--automation-capture-audio            Capture frame-aligned PCM as WAV.\n"
   "--automation-until-disk-idle          Stop after disk motor activity becomes idle.\n"
   "--automation-disk-idle-frames <n>     Required motor-off settling frames (default 50).\n" );
#endif
  printf(
   "\nAvailable command-line options:\n\n"
   "Boolean options (use `--no-<option>' to turn off):\n\n"
   "--auto-load            Automatically load tape files when opened.\n"
   "--compress-rzx         Write RZX files out compressed.\n"
   "--issue2               Emulate an Issue 2 Spectrum.\n"
   "--kempston             Emulate the Kempston joystick on QAOP<space>.\n"
   "--loading-sound        Emulate the sound of tapes loading.\n"
   "--sound                Produce sound.\n"
   "--slt                  Turn SLT traps on.\n"
   "--traps                Turn tape traps on.\n\n"
   "Other options:\n\n"
   "--fbmode <mode>        Which mode should be used for FB?\n"
   "--help                 This information.\n"
   "--machine <type>       Which machine should be emulated?\n"
   "--playback <filename>  Play back RZX file <filename>.\n"
   "--record <filename>    Record to RZX file <filename>.\n"
   "--separation <type>    Use ACB/ABC stereo for the AY-3-8912 sound chip.\n"
   "--snapshot <filename>  Load snapshot <filename>.\n"
   "--speed <percentage>   How fast should emulation run?\n"
   "--tape <filename>      Open tape file <filename>.\n"
   "--version              Print version number and exit.\n"
   "\n"
   "For help, please mail <fuse-emulator-devel@lists.sf.net> or use\n"
   "the forums at <https://sourceforge.net/p/fuse-emulator/discussion/>.\n"
   "For complete documentation, see the manual page of Fuse.\n\n" );
}

/* Stop all activities associated with actual Spectrum emulation */
int fuse_emulation_pause(void)
{
  int error, was_paused;

  /* If we were already paused, just return. In any case, increment
     the pause count */
  was_paused = fuse_emulation_paused++;
  if( was_paused ) return 0;
  keyboard_refresh_invalidate();

  /* Stop recording any competition mode RZX file */
  if( rzx_recording && rzx_competition_mode ) {
    ui_error( UI_ERROR_INFO, "Stopping competition mode RZX recording" );
    error = rzx_stop_recording(); if( error ) return error;
  }
      
  /* If we had sound enabled (and hence doing the speed regulation),
     turn it off */
  sound_pause();

  return 0;
}

/* Restart emulation activities */
int fuse_emulation_unpause(void)
{
  int error;

  /* If this doesn't start us running again, just return. In any case,
     decrement the pause count */
  --fuse_emulation_paused;
  if( fuse_emulation_paused ) return 0;
  keyboard_refresh_invalidate();

  /* If we now want sound, enable it */
  sound_unpause();

  /* Restart speed estimation with no information */
  error = timer_estimate_reset(); if( error ) return error;

  return 0;
}

/* Make 'best guesses' as to what to do with non-option arguments */
static int
parse_nonoption_args( int argc, char **argv, int first_arg,
                      fuse_start_files *start_files )
{
  size_t i, j;
  const char *filename;
  utils_file file;
  libspectrum_id_t type;
  libspectrum_class_t class;

  int error;

#ifdef GEKKO
  /* No argv on the Wii. Just return */
  return 0;
#endif

  for( i = first_arg; i < (size_t)argc; i++ ) {

    filename = argv[i];

    utils_file_init( &file, filename );
    error = utils_file_identify( &file );
    if( error ) {
      utils_file_free( &file );
      return error;
    }

    type = file.type;
    class = file.file_class;

    switch( class ) {

    case LIBSPECTRUM_CLASS_CARTRIDGE_TIMEX:
      utils_file_move( &start_files->dock, &file ); break;

    case LIBSPECTRUM_CLASS_CARTRIDGE_IF2:
      utils_file_move( &start_files->if2, &file ); break;

    case LIBSPECTRUM_CLASS_HARDDISK:
      if( settings_current.zxcf_active ) {
        settings_set_string( &start_files->zxcf, filename );
      } else if( settings_current.zxatasp_active ) {
        settings_set_string( &start_files->zxatasp_master, filename );
      } else if( settings_current.simpleide_active ) {
        settings_set_string( &start_files->simpleide_master, filename );
      } else if( settings_current.divide_enabled ) {
        settings_set_string( &start_files->divide_master, filename );
      } else if( settings_current.divmmc_enabled ) {
        settings_set_string( &start_files->divmmc, filename );
      } else if( settings_current.zxmmc_enabled ) {
        settings_set_string( &start_files->zxmmc, filename );
      } else {
	/* No IDE interface active, so activate the ZXCF */
	settings_current.zxcf_active = 1;
        settings_set_string( &start_files->zxcf, filename );
      }
      break;

    case LIBSPECTRUM_CLASS_DISK_PLUS3:
      utils_file_move( &start_files->disk_plus3, &file ); break;

    case LIBSPECTRUM_CLASS_DISK_OPUS:
      utils_file_move( &start_files->disk_opus, &file ); break;

    case LIBSPECTRUM_CLASS_DISK_DIDAKTIK:
      utils_file_move( &start_files->disk_didaktik80, &file ); break;

    case LIBSPECTRUM_CLASS_DISK_PLUSD:
      if( periph_is_active( PERIPH_TYPE_DISCIPLE ) )
        utils_file_move( &start_files->disk_disciple, &file );
      else
        utils_file_move( &start_files->disk_plusd, &file );
      break;

    case LIBSPECTRUM_CLASS_DISK_TRDOS:
      utils_file_move( &start_files->disk_beta, &file ); break;

    case LIBSPECTRUM_CLASS_DISK_GENERIC:
      if( machine_current->capabilities &
                 LIBSPECTRUM_MACHINE_CAPABILITY_PLUS3_DISK )
        utils_file_move( &start_files->disk_plus3, &file );
      else if( machine_current->capabilities &
                 LIBSPECTRUM_MACHINE_CAPABILITY_TRDOS_DISK )
        utils_file_move( &start_files->disk_beta, &file );
      else {
        if( periph_is_active( PERIPH_TYPE_BETA128 ) )
          utils_file_move( &start_files->disk_beta, &file );
        else if( periph_is_active( PERIPH_TYPE_PLUSD ) )
          utils_file_move( &start_files->disk_plusd, &file );
        else if( periph_is_active( PERIPH_TYPE_DIDAKTIK80 ) )
          utils_file_move( &start_files->disk_didaktik80, &file );
        else if( periph_is_active( PERIPH_TYPE_DISCIPLE ) )
          utils_file_move( &start_files->disk_disciple, &file );
        else if( periph_is_active( PERIPH_TYPE_OPUS ) )
          utils_file_move( &start_files->disk_opus, &file );
      }
      break;

    case LIBSPECTRUM_CLASS_RECORDING:
      utils_file_move( &start_files->playback, &file ); break;

    case LIBSPECTRUM_CLASS_SNAPSHOT:
      utils_file_move( &start_files->snapshot, &file ); break;

    case LIBSPECTRUM_CLASS_MICRODRIVE:
      for( j = 0; j < 8; j++ ) {
        if( !start_files->mdr[j].filename ) {
	  utils_file_move( &start_files->mdr[j], &file );
	  break;
	}
      }
      break;

    case LIBSPECTRUM_CLASS_TAPE:
      utils_file_move( &start_files->tape, &file ); break;

    case LIBSPECTRUM_CLASS_AUXILIARY:
      if( type == LIBSPECTRUM_ID_AUX_POK ) {
        pokemem_set_pokfile( filename );
      }
      break;

    case LIBSPECTRUM_CLASS_UNKNOWN:
      ui_error( UI_ERROR_WARNING, "couldn't identify '%s'; ignoring it",
		filename );
      break;

    default:
      ui_error( UI_ERROR_ERROR, "parse_nonoption_args: unknown file class %d",
		class );
      break;

    }

    utils_close_file( &file );
  }

  return 0;

}

static int
do_start_files( fuse_start_files *start_files )
{
  int autoload, error, i, check_snapshot;

  /* Can't do both input recording and playback */
  if( start_files->playback.filename && start_files->recording ) {
    ui_error(
      UI_ERROR_WARNING,
      "can't do both input playback and recording; recording disabled"
    );
    start_files->recording = NULL;
  }

  /* Can't use both +3 and TR-DOS disks simultaneously */
  if( start_files->disk_plus3.filename && start_files->disk_beta.filename ) {
    ui_error(
      UI_ERROR_WARNING,
      "can't use +3 and TR-DOS disks simultaneously; +3 disk ignored"
    );
    utils_file_free( &start_files->disk_plus3 );
  }

  /* Can't use disks and the dock simultaneously */
  if( ( start_files->disk_plus3.filename ||
        start_files->disk_beta.filename ) &&
      start_files->dock.filename ) {
    ui_error(
      UI_ERROR_WARNING,
      "can't use disks and the dock simultaneously; dock cartridge ignored"
    );
    utils_file_free( &start_files->dock );
  }

  /* Can't use disks and the Interface 2 simultaneously */
  if( ( start_files->disk_plus3.filename ||
        start_files->disk_beta.filename ) &&
      start_files->if2.filename ) {
    ui_error(
      UI_ERROR_WARNING,
      "can't use disks and the Interface 2 simultaneously; cartridge ignored"
    );
    utils_file_free( &start_files->if2 );
  }

  /* If a snapshot has been specified, don't autoload tape, disks etc */
  autoload = start_files->snapshot.filename ? 0 : tape_can_autoload();

  /* Load in each of the files. Input recording must be done after
     snapshot loading such that the right snapshot is embedded into
     the file; input playback being done after snapshot loading means
     any embedded snapshot in the input recording will override any
     specified snapshot */

  if( start_files->disk_plus3.filename ) {
    error = utils_open_loaded_file( &start_files->disk_plus3, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->disk_plusd.filename ) {
    error = utils_open_loaded_file( &start_files->disk_plusd, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->disk_didaktik80.filename ) {
    error = utils_open_loaded_file( &start_files->disk_didaktik80, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->disk_disciple.filename ) {
    error = utils_open_loaded_file( &start_files->disk_disciple, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->disk_opus.filename ) {
    error = utils_open_loaded_file( &start_files->disk_opus, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->disk_beta.filename ) {
    error = utils_open_loaded_file( &start_files->disk_beta, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->dock.filename ) {
    error = utils_open_loaded_file( &start_files->dock, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->if2.filename ) {
    error = utils_open_loaded_file( &start_files->if2, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->snapshot.filename ) {
    error = utils_open_loaded_file( &start_files->snapshot, autoload, NULL );
    if( error ) return error;
  }

  if( start_files->tape.filename ) {
    error = utils_open_loaded_file( &start_files->tape, autoload, NULL );
    if( error ) return error;
  }

  /* Microdrive cartridges */

  for( i = 0; i < 8; i++ ) {
    if( start_files->mdr[i].filename ) {
      error = utils_open_loaded_file( &start_files->mdr[i], autoload, NULL );
      if( error ) return error;
    }
  }

  /* IDE hard disk images */

  if( start_files->simpleide_master ) {
    error = simpleide_insert( start_files->simpleide_master,
			      LIBSPECTRUM_IDE_MASTER );
    simpleide_reset( 0 );
    if( error ) return error;
  }

  if( start_files->simpleide_slave ) {
    error = simpleide_insert( start_files->simpleide_slave,
			      LIBSPECTRUM_IDE_SLAVE );
    simpleide_reset( 0 );
    if( error ) return error;
  }

  if( start_files->zxatasp_master ) {
    error = zxatasp_insert( start_files->zxatasp_master,
			    LIBSPECTRUM_IDE_MASTER );
    if( error ) return error;
  }

  if( start_files->zxatasp_slave ) {
    error = zxatasp_insert( start_files->zxatasp_slave,
			    LIBSPECTRUM_IDE_SLAVE );
    if( error ) return error;
  }

  if( start_files->zxcf ) {
    error = zxcf_insert( start_files->zxcf ); if( error ) return error;
  }

  if( start_files->divide_master ) {
    error = divide_insert( start_files->divide_master,
			    LIBSPECTRUM_IDE_MASTER );
    if( error ) return error;
  }

  if( start_files->divide_slave ) {
    error = divide_insert( start_files->divide_slave,
			    LIBSPECTRUM_IDE_SLAVE );
    if( error ) return error;
  }

  if( start_files->divmmc ) {
    error = divmmc_insert( start_files->divmmc );
    if( error ) return error;
  }

  if( start_files->zxmmc ) {
    error = zxmmc_insert( start_files->zxmmc );
    if( error ) return error;
  }

  /* Input recordings */

  if( start_files->playback.filename ) {
    error = utils_file_read( &start_files->playback );
    if( error ) return error;
#ifdef ENABLE_AUTOMATION
    if( automation_active() ) automation_record_rzx( &start_files->playback );
#endif

    check_snapshot = start_files->snapshot.filename ? 0 : 1;
    error = rzx_start_playback_from_buffer_with_snapshot_check(
      start_files->playback.buffer, start_files->playback.length,
      check_snapshot );
    if( error ) {
#ifdef ENABLE_AUTOMATION
      if( automation_active() ) return 0;
#endif
      return error;
    }
  }

  if( start_files->recording ) {
    error = rzx_start_recording( start_files->recording,
				 settings_current.embed_snapshot );
    if( error ) return error;
  }

  return 0;
}

/* Tidy-up function called at end of emulation */
static int fuse_end(void)
{
  if( legacy_ownership.phase == LEGACY_FRESH ||
      legacy_ownership.phase == LEGACY_STOPPING ||
      legacy_ownership.phase == LEGACY_CLOSED ) return 0;
  /* Internal unwind/emergency cleanup may end STARTING/RUNNING; the ordinary
     shutdown entry deliberately cannot. Disarm before invoking callbacks. */
  legacy_ownership.phase = LEGACY_STOPPING;
  fuse_host_clear_quit();
  fuse_mass_storage_cancel_pending();
  snapshot_cancel_pending();
  rzx_cancel_pending();

  movie_stop();
  /* Active trainer restoration needs live machine memory/maps. */
  pokemem_end();

  if( legacy_ownership.manager_started ) {
    legacy_ownership.manager_started = 0;
    startup_manager_run_end();
  }
  /* The settings callback may not have become a completed registration. */
  fuse_settings_end();

  periph_end();
  fuse_ui_end();
  display_end();
  ui_media_drive_end();
  module_end();
  svg_capture_end();
#ifdef ENABLE_AUTOMATION
  automation_end();
#endif

  if( legacy_ownership.xml_owned ) {
    legacy_ownership.xml_owned = 0;
#ifdef HAVE_LIB_XML2
    xmlCleanupParser();
#endif
  }
  if( legacy_ownership.libspectrum_owned ) {
    legacy_ownership.libspectrum_owned = 0;
    libspectrum_end();
  }
  legacy_ownership.save_settings = 0;
  if( legacy_ownership.callback_owned ) {
    legacy_ownership.callback_owned = 0;
    libspectrum_error_function = legacy_ownership.previous_error_function;
    legacy_ownership.previous_error_function = NULL;
  }
  fuse_host_end_resources();
  legacy_ownership.phase = LEGACY_CLOSED;

  return 0;
}

#ifdef FUSE_STARTUP_TEST
void
fuse_startup_test_shutdown( void )
{
  fuse_end();
}
#endif

/* Emergency shutdown */
void
fuse_abort( void )
{
  fuse_end();
  abort();
}
