/* Private startup ownership checks; each runner attempt uses a fresh process. */
#include "config.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuse.h"
#include "fuse-internal.h"
#include "infrastructure/startup_files.h"
#include "settings.h"
#ifdef ENABLE_AUTOMATION
#include "automation/automation.h"
#endif

static void
assert_settings_cleared( settings_info *settings )
{
  size_t i;
  char **rom;
  for( i = 0; ( rom = settings_get_rom_setting( settings, i, 0 ) ); i++ )
    assert( !*rom );
  assert( !settings->snapshot );
  assert( !settings->sound_device );
}

static void
test_storage( void )
{
  settings_info local = { 0 };
  fuse_start_files files = { 0 };
  char positional[] = "positional.hdf";
  char **setting_paths[] = {
    &settings_current.simpleide_master_file,
    &settings_current.simpleide_slave_file,
    &settings_current.zxatasp_master_file,
    &settings_current.zxatasp_slave_file,
    &settings_current.zxcf_pri_file,
    &settings_current.divide_master_file,
    &settings_current.divide_slave_file,
    &settings_current.divmmc_file,
    &settings_current.zxmmc_file
  };
  char **paths[] = {
    &files.simpleide_master, &files.simpleide_slave,
    &files.zxatasp_master, &files.zxatasp_slave, &files.zxcf,
    &files.divide_master, &files.divide_slave, &files.divmmc, &files.zxmmc
  };
  char **setting_files[] = {
    &settings_current.plus3disk_file, &settings_current.opusdisk_file,
    &settings_current.plusddisk_file, &settings_current.betadisk_file,
    &settings_current.didaktik80disk_file, &settings_current.discipledisk_file,
    &settings_current.dck_file, &settings_current.if2_file,
    &settings_current.playback_file, &settings_current.snapshot,
    &settings_current.tape_file, &settings_current.mdr_file,
    &settings_current.mdr_file2, &settings_current.mdr_file3,
    &settings_current.mdr_file4, &settings_current.mdr_file5,
    &settings_current.mdr_file6, &settings_current.mdr_file7,
    &settings_current.mdr_file8
  };
  utils_file *inputs[] = {
    &files.disk_plus3, &files.disk_opus, &files.disk_plusd, &files.disk_beta,
    &files.disk_didaktik80, &files.disk_disciple, &files.dock, &files.if2,
    &files.playback, &files.snapshot, &files.tape,
    &files.mdr[0], &files.mdr[1], &files.mdr[2], &files.mdr[3],
    &files.mdr[4], &files.mdr[5], &files.mdr[6], &files.mdr[7]
  };
  size_t i;

  settings_defaults( &local );
  settings_free( &local );
  settings_free( &local );
  assert_settings_cleared( &local );

  fuse_start_files_end( &files );
  fuse_start_files_end( &files );
  settings_defaults( &settings_current );
  for( i = 0; i < sizeof( paths ) / sizeof( *paths ); i++ )
    settings_set_string( setting_paths[i], "settings.hdf" );
  for( i = 0; i < sizeof( inputs ) / sizeof( *inputs ); i++ )
    settings_set_string( setting_files[i], "input.dat" );
  settings_set_string( &settings_current.record_file, "recording.rzx" );

  fuse_start_files_init( &files );
  assert( files.recording == settings_current.record_file );
  for( i = 0; i < sizeof( paths ) / sizeof( *paths ); i++ ) {
    assert( *paths[i] != *setting_paths[i] );
    assert( !strcmp( *paths[i], "settings.hdf" ) );
    /* Mirrors positional replacement, including replacing an earlier value. */
    settings_set_string( paths[i], positional );
    settings_set_string( paths[i], positional );
    assert( *paths[i] != positional );
    settings_set_string( setting_paths[i], "replaced-settings.hdf" );
    assert( !strcmp( *paths[i], "positional.hdf" ) );
  }
  for( i = 0; i < sizeof( inputs ) / sizeof( *inputs ); i++ ) {
    assert( inputs[i]->filename != *setting_files[i] );
    assert( !strcmp( inputs[i]->filename, "input.dat" ) );
    /* Partially loaded inputs own buffers as well as filenames. */
    inputs[i]->buffer = libspectrum_new( libspectrum_byte, 16 );
    inputs[i]->length = 16;
  }
  fuse_start_files_end( &files );
  fuse_start_files_end( &files );
  for( i = 0; i < sizeof( paths ) / sizeof( *paths ); i++ ) assert( !*paths[i] );
  for( i = 0; i < sizeof( inputs ) / sizeof( *inputs ); i++ ) {
    assert( !inputs[i]->filename );
    assert( !inputs[i]->buffer );
  }
  assert( !files.recording );
  assert( !strcmp( settings_current.record_file, "recording.rzx" ) );

  /* Cleanup is also valid when setup stopped before all fields were filled. */
  files.zxcf = utils_safe_strdup( positional );
  utils_file_init( &files.tape, "partial.tap" );
  fuse_start_files_end( &files );
  fuse_start_files_end( &files );
  settings_free( &settings_current );
  settings_free( &settings_current );
}

static libspectrum_error
preserved_error( libspectrum_error error, const char *format, va_list ap )
{
  (void)format;
  (void)ap;
  return error;
}

static void
test_settings_end( int save )
{
  int first_arg;
  char program[] = "ownership-test", help[] = "--help";
  char autosave[] = "--autosave-settings";
  char *args[] = { program, help, autosave, NULL };

  assert( !settings_init( &first_arg, 3, args ) );
  assert( settings_current.autosave_settings );
  settings_end( save );
  assert_settings_cleared( &settings_current );
  settings_end( 1 ); /* A second end cannot acquire permission to save again. */
}

static void
run_and_check( int argc, char **args, int expected )
{
  libspectrum_error_function = preserved_error;
  assert( fuse_legacy_main( argc, args ) == expected );
  assert( libspectrum_error_function == preserved_error );
  assert_settings_cleared( &settings_current );
  settings_end( 1 );
#ifdef ENABLE_AUTOMATION
  assert( !automation_active() );
  automation_end();
  automation_end();
#endif
  /* No restart/reentry promise: reject another attempt before touching state. */
  assert( fuse_legacy_main( argc, args ) == 1 );
  assert( libspectrum_error_function == preserved_error );
}

static void
test_runner( const char *option, int expected )
{
  char program[] = "ownership-test", autosave[] = "--autosave-settings";
  char freq[] = "--sound-freq", value[] = "12345", selected[64];
  char *args[] = { program, autosave, freq, value, selected, NULL, NULL, NULL, NULL };
  int argc = 5;

  snprintf( selected, sizeof( selected ), "%s", option );
#ifdef ENABLE_AUTOMATION
  char output[] = "--automation-output", directory[] = ".";
  char frames[] = "--automation-frames", zero[] = "0";
  if( expected ) {
    args[4] = output; args[5] = directory;
    args[6] = frames; args[7] = zero;
    argc = 8;
  }
#endif
  run_and_check( argc, args, expected );
}

int
main( int argc, char **argv )
{
  assert( argc >= 2 );
  fuse_progname = argv[0];
  if( !strcmp( argv[1], "storage" ) ) test_storage();
  else if( !strcmp( argv[1], "no-save" ) ) test_settings_end( 0 );
  else if( !strcmp( argv[1], "save" ) ) test_settings_end( 1 );
  else if( !strcmp( argv[1], "help" ) ) test_runner( "--help", 0 );
  else if( !strcmp( argv[1], "version" ) ) test_runner( "--version", 0 );
#ifdef ENABLE_AUTOMATION
  else if( !strcmp( argv[1], "settings-failure" ) ) test_runner( "", 1 );
  else if( !strcmp( argv[1], "session" ) ) run_and_check( argc - 1, argv + 1, 0 );
#endif
  else assert( 0 );
  return 0;
}
