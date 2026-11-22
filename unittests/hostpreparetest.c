/* Private configuration/resource preparation contract, run in child processes. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-engine.h"
#include "fuse-legacy.h"
#include "machine.h"
#include "settings.h"
#include "snapshot.h"
#include "memory_pages.h"
#include "event.h"
#include "ui/ui.h"

extern int null_ui_error_specific_calls;
static int startup_notices, open_errors, run_notices, notifications;

static void
receive_diagnostic( void *context, fuse_diagnostic_level level,
                    const char *message )
{
  assert( context == &startup_notices );
  notifications++;
  if( !strcmp( message, "host startup probe" ) ) {
    assert( level == FUSE_DIAGNOSTIC_WARNING );
    startup_notices++;
  } else if( strstr( message, "missing-diagnostic-input.tap" ) ) {
    assert( level == FUSE_DIAGNOSTIC_ERROR );
    open_errors++;
  } else if( !strcmp( message, "host run probe" ) ) {
    assert( level == FUSE_DIAGNOSTIC_INFO );
    run_notices++;
  }
}

static void
run_diagnostic( libspectrum_dword when GCC_UNUSED, int type GCC_UNUSED,
                void *data GCC_UNUSED )
{
  ui_error_console( UI_ERROR_INFO, "host run probe" );
}

static struct {
  const char *path;
  const char *mode;
  unsigned char *bytes;
  int acquired, released;
} provider;

static void
release_rom( void *context )
{
  assert( context == &provider );
  assert( provider.acquired == provider.released + 1 );
  free( provider.bytes );
  provider.bytes = NULL;
  provider.released++;
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  FILE *file;
  assert( context == &provider );
  assert( !strcmp( name, "48.rom" ) );
  assert( provider.acquired == provider.released );
  provider.acquired++;
  if( !strcmp( provider.mode, "success" ) || !strcmp( provider.mode,
                                                      "missing" ) )
    ui_error( UI_ERROR_WARNING, "host startup probe" );
  assert( fuse_host_prepare( NULL, NULL ) == FUSE_INIT_BUSY );
  fuse_shutdown(); /* Must not tear down STARTING. */
  rom->context = &provider;
  rom->release = release_rom;
  if( !strcmp( provider.mode, "missing" ) ) return 1;
  provider.bytes = malloc( 16384 );
  assert( provider.bytes );
  file = fopen( provider.path, "rb" );
  assert( file );
  assert( fread( provider.bytes, 1, 16384, file ) == 16384 );
  assert( !fclose( file ) );
  rom->data = provider.bytes;
  rom->length = !strcmp( provider.mode, "bad-size" ) ? 1 : 16384;
  rom->custom = !strcmp( provider.mode, "custom" );
  if( !strcmp( provider.mode, "bad-provenance" ) ) rom->custom = 2;
  if( !strcmp( provider.mode, "null-bytes" ) ) rom->data = NULL;
  return !strcmp( provider.mode, "error-bytes" );
}

int
host_prepare_test( int argc, char **argv )
{
  fuse_host_config *config;
  fuse_host_resources resources = { &provider, acquire_rom };
  fuse_init_result result;
  char machine[] = "48";
  static const joystick_type_t expected_interfaces[] = {
    JOYSTICK_TYPE_NONE, JOYSTICK_TYPE_CURSOR, JOYSTICK_TYPE_KEMPSTON,
    JOYSTICK_TYPE_SINCLAIR_1, JOYSTICK_TYPE_SINCLAIR_2, JOYSTICK_TYPE_TIMEX_1,
    JOYSTICK_TYPE_TIMEX_2, JOYSTICK_TYPE_FULLER
  };
  int success, interface_index = -1, slot;
  assert( argc == 3 );
  provider.mode = argv[1];
  provider.path = argv[2];
  if( !strcmp( provider.mode, "legacy-state" ) ) {
    char program[] = "host-reference", sound[] = "--no-sound";
    char *args[] = { program, sound, NULL };
    assert( fuse_legacy_prepare( 2, args ) == FUSE_INIT_READY );
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
    assert( !snapshot_write( "state.szx" ) );
    fuse_shutdown();
    return 0;
  }
  if( !strncmp( provider.mode, "joystick-", 9 ) ) {
    assert( sscanf( provider.mode, "joystick-%d", &interface_index ) == 1 );
    assert( interface_index >= 0 && interface_index < JOYSTICK_TYPE_COUNT );
  }
  success = interface_index >= 0 || !strcmp( provider.mode, "success" ) ||
            !strcmp( provider.mode, "host-state" ) ||
            !strcmp( provider.mode, "custom" );
  assert( fuse_host_prepare( NULL, &resources ) == FUSE_INIT_INVALID_ARGUMENT );
  config = fuse_host_config_alloc();
  assert( fuse_host_config_set_machine( NULL, machine ) );
  assert( fuse_host_config_set_machine( config, "" ) );
  assert( fuse_host_config_set_sound( config, 2 ) );
  assert( !fuse_host_config_set_machine( config, machine ) );
  machine[0] = 'x'; /* Configuration must not borrow the caller's string. */
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( !fuse_host_config_set_diagnostics( config, receive_diagnostic, &startup_notices ) );
  assert( fuse_host_config_set_joystick( NULL, FUSE_HOST_JOYSTICK_1, JOYSTICK_TYPE_NONE ) );
  if( interface_index >= 0 ) {
    for( slot = FUSE_HOST_JOYSTICK_1; slot <= FUSE_HOST_JOYSTICK_KEYBOARD; slot++ ) {
      assert( !fuse_host_config_set_joystick( config, (fuse_host_joystick)slot,
                                             expected_interfaces[interface_index] ) );
      assert( fuse_host_config_set_joystick( config, (fuse_host_joystick)slot,
                                            (joystick_type_t)-1 ) );
      assert( fuse_host_config_set_joystick( config, (fuse_host_joystick)slot,
                                            (joystick_type_t)JOYSTICK_TYPE_COUNT ) );
    }
    assert( fuse_host_config_set_joystick( config, (fuse_host_joystick)-1, JOYSTICK_TYPE_KEMPSTON ) );
    assert( fuse_host_config_set_joystick( config, (fuse_host_joystick)3, JOYSTICK_TYPE_KEMPSTON ) );
  }
  assert( fuse_host_prepare( config, NULL ) == FUSE_INIT_INVALID_ARGUMENT );
  {
    fuse_host_resources invalid = { &provider, NULL };
    assert( fuse_host_prepare( config, &invalid ) == FUSE_INIT_INVALID_ARGUMENT );
  }
  result = fuse_host_prepare( config, &resources );
  fuse_host_config_free( config );
  fuse_host_config_free( NULL );
  assert( provider.acquired == 1 && provider.released == 1 );
  if( success ) {
    assert( result == FUSE_INIT_READY );
    assert( !strcmp( settings_current.start_machine, "48" ) );
    assert( !settings_current.sound );
    if( interface_index >= 0 ) {
      /* Numeric values are persisted settings, not a second identity scheme. */
      assert( settings_current.joystick_1_output == interface_index );
      assert( settings_current.joystick_2_output == interface_index );
      assert( settings_current.joystick_keyboard_output == interface_index );
    }
    assert( machine_current );
    assert( !!memory_custom_rom() == !strcmp( provider.mode, "custom" ) );
    if( !strcmp( provider.mode, "custom" ) ) {
      libspectrum_snap *snap = libspectrum_snap_alloc();
      assert( !snapshot_copy_to( snap ) );
      assert( libspectrum_snap_custom_rom( snap ) );
      assert( !snapshot_copy_from( snap ) );
      assert( !libspectrum_snap_free( snap ) );
      /* Embedded snapshot ROMs precede the provider; no second acquisition. */
      assert( provider.acquired == 1 && provider.released == 1 );
    }
    assert( fuse_legacy_prepare( 0, NULL ) == FUSE_INIT_BUSY );
    if( !strcmp( provider.mode, "success" ) ) {
      fuse_open_request request;
      int diagnostic_event = event_register( run_diagnostic,
                                             "host diagnostic" );
      assert( startup_notices == 1 );
      assert( fuse_host_open_file( "missing-diagnostic-input.tap", &request ) ==
              FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( open_errors == 1 && !request.id );
      event_add( 0, diagnostic_event );
    }
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
    if( !strcmp( provider.mode, "success" ) ) assert( run_notices == 1 );
    if( !strcmp( provider.mode, "host-state" ) )
      assert( !snapshot_write( "state.szx" ) );
  } else {
    assert( result == FUSE_INIT_FAILED );
    if( !strcmp( provider.mode, "missing" ) ) assert( startup_notices == 1 );
  }
  fuse_shutdown();
  fuse_shutdown();
  assert( !fuse_host_resources_active() );
  assert( !null_ui_error_specific_calls );
  {
    int before = notifications;
    ui_error( UI_ERROR_INFO, "after host teardown" );
    assert( null_ui_error_specific_calls == 1 && notifications == before );
  }
  assert( fuse_host_prepare( NULL, NULL ) == FUSE_INIT_CLOSED );
  return 0;
}
