/* Instrumented real-engine startup/unwind consumer; no production test API. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "automation/automation.h"
#include "display.h"
#include "fuse.h"
#include "fuse-internal.h"
#include "infrastructure/startup_test.h"
#include "machine.h"
#include "settings.h"
#include "sound.h"
#include "sound/blipbuffer.h"
#include "peripherals/disk/fdd.h"
#include "ui/uimedia.h"

extern Blip_Buffer *left_buf, *right_buf;
extern blip_sample_t *samples;

#define MODULE_COUNT (STARTUP_MANAGER_MODULE_ZXMMC + 1)
static startup_manager_module expected[MODULE_COUNT];
static int depth, target_module = -1, after, injected, occurrence = 1;
static int point_visits, model_visits, natural_failure;
static const char *target_point;
static int media_test, record_test;
static ui_media_drive_info_t *media_drive;

int
fuse_startup_test_checkpoint( startup_manager_module module, int completed,
                              int has_end )
{
  int status = 73;
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
  assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
  fuse_legacy_shutdown(); /* STARTING must not dispose an initializer's state. */
  if( natural_failure ) settings_current.autosave_settings = 1;
  if( media_test && completed && module == STARTUP_MANAGER_MODULE_MACHINE ) {
    media_drive = ui_media_drive_find( UI_MEDIA_CONTROLLER_PLUS3, 0 );
    assert( media_drive && media_drive->fdd );
    assert( !media_drive->fdd->disk.data && !media_drive->fdd->disk.filename );
    media_drive->fdd->disk.data = libspectrum_new( libspectrum_byte, 512 );
    media_drive->fdd->disk.filename = utils_safe_strdup( "owned-startup.dsk" );
  }
  if( completed && has_end ) {
    assert( depth < MODULE_COUNT );
    expected[depth++] = module;
  }
  if( module == target_module && completed == after ) {
    injected = 1;
    settings_current.autosave_settings = 1;
    return 1;
  }
  return 0;
}

void
fuse_startup_test_cleanup( startup_manager_module module )
{
  assert( depth > 0 );
  assert( expected[--depth] == module );
  if( record_test && module == STARTUP_MANAGER_MODULE_RZX ) {
    int creator_live = 0, settings_live = 0, i;
    for( i = 0; i < depth; i++ ) {
      creator_live |= expected[i] == STARTUP_MANAGER_MODULE_CREATOR;
      settings_live |= expected[i] == STARTUP_MANAGER_MODULE_SETTINGS_END;
    }
    assert( creator_live && settings_live );
  }
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
  fuse_legacy_shutdown(); /* STOPPING must not recursively enter teardown. */
  /* A cleanup callback must not restart either teardown layer. */
  fuse_startup_test_shutdown();
  startup_manager_run_end();
}

int
fuse_startup_test_fail( const char *point )
{
  if( !strcmp( point, "machine.model" ) ) model_visits++;
  if( target_point && !strcmp( target_point, point ) &&
      ++point_visits == occurrence ) {
    injected = 1;
    if( strncmp( point, "sound.", 6 ) ) settings_current.autosave_settings = 1;
    return 1;
  }
  return 0;
}

void
fuse_startup_test_running( void )
{
  int status = 73;
  assert( fuse_legacy_prepare( 0, NULL ) == FUSE_LEGACY_INIT_BUSY );
  assert( fuse_legacy_execute( &status ) == 1 && status == 73 );
  fuse_legacy_shutdown(); /* RUNNING must keep machine/settings alive. */
  assert( machine_current && settings_current.rom_48 );
}

static libspectrum_error
preserved_error( libspectrum_error error, const char *format, va_list ap )
{
  (void)format;
  (void)ap;
  return error;
}

int
main( int argc, char **argv )
{
  if( argc == 2 && !strcmp( argv[1], "count" ) ) {
    printf( "%d\n", MODULE_COUNT );
    return 0;
  }
  assert( argc == 4 || argc == 5 );
  const char *kind = argv[1], *target = argv[2];
  char program[] = "startup-unwind", machine[] = "--machine", model[] = "48";
  char rom[] = "--rom-48", no_save[] = "--no-autosave-settings";
  char output[] = "--automation-output", directory[] = ".";
  char frames[] = "--automation-frames", one[] = "1";
  char capture[] = "--automation-capture-audio", separation[] = "--separation";
  char stereo[] = "ACB", filter[] = "--graphics-filter", invalid[] = "invalid";
  char snapshot[] = "--snapshot", missing[] = "missing-startup-input";
  char bad_rom[] = "invalid-startup.rom";
  char record[] = "--record", recording[] = "owned-startup.rzx";
  char *args[] = { program, machine, model, rom, argv[3], no_save,
                  output, directory, frames, one, NULL, NULL, NULL, NULL, NULL };
  int count = 10;
  if( argc == 5 ) occurrence = atoi( argv[4] );
  if( !strcmp( kind, "before" ) || !strcmp( kind, "after" ) ) {
    target_module = atoi( target );
    assert( target_module >= 0 && target_module < MODULE_COUNT );
    after = !strcmp( kind, "after" );
  } else if( !strcmp( kind, "media" ) ) {
    target_module = STARTUP_MANAGER_MODULE_MACHINE;
    after = media_test = 1;
  } else if( !strcmp( kind, "record" ) ) {
    record_test = 1;
    target_point = "startup.files";
    args[count++] = record; args[count++] = recording;
  } else if( !strcmp( kind, "point" ) ) {
    target_point = target;
    if( !strncmp( target, "sound.", 6 ) ) {
      args[count++] = capture;
      args[count++] = separation;
      args[count++] = stereo;
    }
  } else if( !strcmp( kind, "natural" ) ) {
    natural_failure = 1;
    if( !strcmp( target, "rom" ) ) {
      FILE *file = fopen( bad_rom, "wb" );
      assert( file );
      assert( fwrite( "bad", 1, 3, file ) == 3 );
      assert( !fclose( file ) );
      args[4] = bad_rom;
      /* Prevent an installed default ROM from masking the invalid-size path. */
      settings_default.rom_48 = bad_rom;
    }
    else if( !strcmp( target, "scaler" ) ) {
      args[count++] = filter; args[count++] = invalid;
    } else if( !strcmp( target, "snapshot" ) ) {
      args[count++] = snapshot; args[count++] = missing;
    } else assert( !strcmp( target, "display" ) );
  } else assert( !strcmp( kind, "models" ) );

  fuse_startup_test_shutdown();
  fuse_startup_test_shutdown();
  libspectrum_error_function = preserved_error;
  int result = fuse_legacy_main( count, args );
  assert( !depth );
  assert( libspectrum_error_function == preserved_error );
  assert( !machine_types && !machine_count && !machine_current );
  assert( !settings_current.rom_48 && !settings_current.sound_device );
  assert( !sound_enabled && !left_buf && !right_buf && !samples );
  assert( !automation_active() && !display_ui_initialised );
  if( media_test )
    assert( !media_drive->fdd->disk.data && !media_drive->fdd->disk.filename );
  if( record_test ) {
    char magic[4];
    FILE *file = fopen( recording, "rb" );
    assert( file && fread( magic, 1, 4, file ) == 4 );
    assert( !memcmp( magic, "RZX!", 4 ) );
    assert( !fclose( file ) );
  }
  fuse_startup_test_shutdown();
  fuse_startup_test_shutdown();
  assert( !depth );

  if( !strcmp( kind, "models" ) ) {
    assert( !result );
    printf( "%d\n", model_visits );
    return 0;
  }
  if( !strcmp( kind, "natural" ) ) assert( result == 1 );
  else if( injected ) {
    /* Audio setup retains its legacy nonfatal fallback to disabled output. */
    int sound_failure = target_point && !strncmp( target_point, "sound.", 6 );
    assert( result == ( sound_failure ? 0 : 1 ) );
  }
  else {
    assert( !result );
    fprintf( stderr, "SKIP: startup point not configured: %s %s\n", kind, target );
    return 77;
  }
  return 0;
}
