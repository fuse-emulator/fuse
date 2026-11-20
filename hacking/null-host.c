/* Bounded direct consumer of the private, selected-null-UI worker API.
   Not a toolkit-free core, command server or replacement for the legacy CLI.
   Build/run instructions and limits: hacking/legacy-engine.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuse-internal.h"

typedef struct rom_provider {
  const char *directory;
  unsigned char *pending;
  unsigned int requests, releases;
} rom_provider;

static void
release_rom( void *context )
{
  rom_provider *provider = context;
  free( provider->pending );
  provider->pending = NULL;
  provider->releases++;
}

/* This host treats directory contents as its standard ROM set. Another host
   could supply packaged bytes here; the engine does not choose a filesystem
   search path or retry a missing ROM behind the host's back. */
static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  rom_provider *provider = context;
  char path[4096];
  FILE *file;
  long length;
  int written, failed;

  provider->requests++;
  rom->context = provider;
  rom->release = release_rom; /* Called even when acquisition/validation fails. */
  written = snprintf( path, sizeof(path), "%s/%s", provider->directory, name );
  if( written < 0 || (size_t)written >= sizeof(path) ) {
    fprintf( stderr, "host: ROM path is too long: %s\n", name );
    return 1;
  }
  file = fopen( path, "rb" );
  if( !file ) {
    fprintf( stderr, "host: cannot open ROM: %s\n", path );
    return 1;
  }
  if( fseek( file, 0, SEEK_END ) || ( length = ftell( file ) ) <= 0 ||
      fseek( file, 0, SEEK_SET ) ) {
    fprintf( stderr, "host: cannot size ROM: %s\n", path );
    fclose( file );
    return 1;
  }
  provider->pending = malloc( (size_t)length );
  failed = !provider->pending ||
           fread( provider->pending, 1, (size_t)length, file ) != (size_t)length;
  if( fclose( file ) ) failed = 1;
  if( failed ) {
    fprintf( stderr, "host: cannot read ROM: %s\n", path );
    return 1; /* release_rom owns any partial allocation. */
  }
  rom->data = provider->pending;
  rom->length = (size_t)length;
  rom->custom = 0; /* Explicit host provenance: standard ROM, not a custom override. */
  return 0;
}

#include "memory-filesystem.h"

/* Platform input import only; all engine media operations use the installed
   filesystem, not resource variants or emulator serialization in this host. */
static int
import_file( const char *path, const char *name )
{
  unsigned char buffer[4096];
  size_t length;
  int failed = 0;
  FILE *input = fopen( path, "rb" );
  compat_fd output;
  if( !input ) return 1;
  output = compat_file_open( name, COMPAT_FILE_WRITE );
  if( !output ) { fclose( input ); return 1; }
  while( ( length = fread( buffer, 1, sizeof(buffer), input ) ) )
    if( compat_file_write( output, buffer, length ) ) { failed = 1; break; }
  if( ferror( input ) ) failed = 1;
  if( fclose( input ) ) failed = 1;
  if( compat_file_close( output ) ) failed = 1;
  return failed;
}

static int
operation( const char *name, fuse_host_operation_result result )
{
  if( result == FUSE_HOST_OPERATION_OK ) return 0;
  fprintf( stderr, "host: %s failed (operation result %d)\n", name, (int)result );
  return 1;
}

static void
print_disk_info( const char *label, const fuse_disk_drive_info *info )
{
  printf( "host: %s; controller=%d; slot=%d; name=%s; available=%d; enabled=%d; loaded=%d; flipped=%d; protected=%d; target=%s\n",
          label, (int)info->controller, info->slot, info->name ? info->name : "(none)",
          info->available, info->enabled, info->loaded, info->flipped,
          info->write_protected, info->filename ? info->filename : "(none)" );
}

static void
print_tape_info( const char *label, const fuse_tape_info *info )
{
  size_t i;
  printf( "host: %s; present=%d; playing=%d; recording=%d; modified=%d; current=%d; blocks=%zu\n",
          label, info->present, info->playing, info->recording, info->modified,
          info->current_block, info->block_count );
  for( i = 0; i < info->block_count; i++ )
    printf( "host: tape row %zu; %s; %s\n", i,
            info->blocks[i].description, info->blocks[i].details );
}

static int
frames( unsigned int count, unsigned int *completed )
{
  unsigned int i;
  for( i = 0; i < count; i++ ) {
    fuse_run_result result = fuse_run_frame();
    if( result != FUSE_RUN_COMPLETED ) {
      fprintf( stderr, "host: frame turn stopped (run result %d)\n", (int)result );
      return 1;
    }
    (*completed)++;
  }
  return 0;
}

int
main( int argc, char **argv )
{
  rom_provider provider = { 0 };
  fuse_host_resources resources = { &provider, acquire_rom };
  fuse_host_config *config;
  fuse_disk_drive_info *disk = NULL, *saved_disk = NULL;
  fuse_tape_info *tape_info = NULL, *saved_tape_info = NULL;
  fuse_init_result init;
  fuse_run_result run;
  unsigned int frame_turns = 0, budget_turns = 0;
  int prepared = 0, status = EXIT_FAILURE;
  int disk_lifecycle = argc == 4 && !strcmp( argv[2], "--disk-lifecycle" );
  int tape_record = argc == 4 && !strcmp( argv[2], "--record-tape" );
  int snapshot_save = argc == 4 && !strcmp( argv[2], "--save-snapshot" );
  int filesystem = argc == 4 && !strcmp( argv[2], "--filesystem" );
  int divmmc = argc == 4 && !strcmp( argv[2], "--divmmc" );
  int filesystem_installed = 0;
  memory_filesystem memory = { 0 };
  int disk_save = argc == 5 && !strcmp( argv[2], "--save-disk" );
  int tape_lifecycle = argc == 5 && !strcmp( argv[2], "--tape-lifecycle" );

  if( ( argc != 2 && argc != 3 && argc != 4 && argc != 5 ) ||
      ( argc == 4 && strcmp( argv[2], "--open" ) && !disk_lifecycle && !tape_record && !snapshot_save && !filesystem && !divmmc ) ||
      ( argc == 5 && !disk_save && !tape_lifecycle ) ||
      ( argc == 3 && ( !strcmp( argv[2], "--open" ) || !strcmp( argv[2], "--save-disk" ) ||
                      !strcmp( argv[2], "--disk-lifecycle" ) || !strcmp( argv[2], "--tape-lifecycle" ) ||
                      !strcmp( argv[2], "--record-tape" ) || !strcmp( argv[2], "--save-snapshot" ) || !strcmp( argv[2], "--filesystem" ) || !strcmp( argv[2], "--divmmc" ) ) ) ) {
    fprintf( stderr, "Usage: %s ROM_DIRECTORY [TAPE_FILE | --open FILE | --save-disk INPUT OUTPUT | --disk-lifecycle OUTPUT | --tape-lifecycle INPUT OUTPUT | --record-tape OUTPUT | --save-snapshot OUTPUT | --filesystem TAPE_FILE | --divmmc CARD_FILE]\n", argv[0] );
    return EXIT_FAILURE;
  }
  if( filesystem || divmmc ) {
    memory.chunk = 7; /* Exercise successful short transfers at the boundary. */
    if( memory_filesystem_install( &memory ) ) goto cleanup;
    filesystem_installed = 1;
    if( import_file( argv[3], divmmc ? "media/card.hdf" : "media/input.tap" ) ) {
      fputs( "host: filesystem input import failed\n", stderr ); goto cleanup;
    }
  }
  provider.directory = argv[1]; /* Borrowed through shutdown, not passed as engine CLI. */
  config = fuse_host_config_alloc();
  if( fuse_host_config_set_machine( config, disk_save || disk_lifecycle ? "plus3" : "48" ) ||
      fuse_host_config_set_sound( config, 0 ) ||
      ( tape_record && fuse_host_config_set_tape_traps( config, 0 ) ) ||
      ( filesystem && fuse_host_config_set_auto_load( config, 0 ) ) ||
      ( divmmc && fuse_host_config_set_divmmc( config, 1 ) ) ||
      fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_1,
                                    JOYSTICK_TYPE_KEMPSTON ) ) {
    fprintf( stderr, "host: configuration rejected\n" );
    fuse_host_config_free( config );
    goto cleanup;
  }
  init = fuse_host_prepare( config, &resources );
  fuse_host_config_free( config ); /* Preparation owns its copied configuration. */
  if( init != FUSE_INIT_READY ) {
    fprintf( stderr, "host: preparation failed (init result %d)\n", (int)init );
    goto cleanup; /* A rejected prepare does not authorize another session's shutdown. */
  }
  prepared = 1;
  puts( disk_save || disk_lifecycle ? "host: prepared plus3" : "host: prepared 48" );
  if( divmmc ) {
    fuse_mass_storage_info *drive = NULL;
    int mounted;
    if( operation( "insert DivMMC", fuse_host_insert_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, "media/card.hdf" ) ) ||
        operation( "observe DivMMC", fuse_host_copy_mass_storage_info( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, &drive ) ) ) goto cleanup;
    mounted = drive->active && drive->backing_name && !strcmp( drive->backing_name, "media/card.hdf" );
    if( !mounted || operation( "commit DivMMC", fuse_host_commit_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) ) ||
        operation( "eject DivMMC", fuse_host_eject_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) ) ) {
      fuse_mass_storage_info_free( drive );
      goto cleanup;
    }
    fuse_mass_storage_info_free( drive );
    puts( "host: DivMMC lifecycle; handles balanced" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( filesystem ) {
    if( operation( "filesystem Tape Open", fuse_host_open_file( "media/input.tap" ) ) ||
        operation( "filesystem tape save", fuse_host_save_tape_file( "media/copy.pzx" ) ) ||
        operation( "tape eject", fuse_host_eject_tape() ) ||
        operation( "filesystem tape reopen", fuse_host_open_file( "media/copy.pzx" ) ) ||
        frames( 2, &frame_turns ) ||
        operation( "filesystem snapshot save", fuse_host_save_snapshot_file( "media/state.szx" ) ) ||
        operation( "reset", fuse_host_reset( FUSE_HOST_RESET_HARD ) ) ||
        operation( "filesystem snapshot reopen", fuse_host_open_file( "media/state.szx" ) ) ||
        frames( 1, &frame_turns ) ) goto cleanup;
    {
      unsigned char hdf[640] = { 'R','S','-','I','D','E',0x1a,0x11,0,128,0 };
      fuse_mass_storage_info *drive = NULL;
      int ok, error;
      compat_fd backing;
      hdf[24] = hdf[28] = hdf[34] = 1;
      backing = compat_file_open( "media/drive.hdf", COMPAT_FILE_WRITE );
      if( !backing ) goto cleanup;
      error = compat_file_write( backing, hdf, sizeof(hdf) );
      if( compat_file_close( backing ) ) error = 1;
      if( error ||
          operation( "open ZXCF", fuse_host_open_file( "media/drive.hdf" ) ) ||
          operation( "observe ZXCF", fuse_host_copy_mass_storage_info( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &drive ) ) )
        goto cleanup;
      ok = drive->active && drive->backing_name && !strcmp( drive->backing_name, "media/drive.hdf" );
      fuse_mass_storage_info_free( drive );
      if( !ok || operation( "commit ZXCF", fuse_host_commit_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) ) ||
          operation( "eject ZXCF", fuse_host_eject_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) ) ) goto cleanup;
    }
    puts( "host: effective filesystem; tape/snapshot roundtrip; ZXCF lifecycle; handles balanced" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( snapshot_save ) {
    if( frames( 2, &frame_turns ) ||
        operation( "save snapshot", fuse_host_save_snapshot_file( argv[3] ) ) ||
        operation( "reset", fuse_host_reset( FUSE_HOST_RESET_HARD ) ) ||
        operation( "reopen snapshot", fuse_host_open_file( argv[3] ) ) ||
        frames( 1, &frame_turns ) ) goto cleanup;
    puts( "host: saved snapshot; reset and reopened" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( tape_record ) {
    if( operation( "start tape recording", fuse_host_start_tape_recording() ) ||
        operation( "recording info", fuse_host_copy_tape_info( &tape_info ) ) ) goto cleanup;
    print_tape_info( "recording tape copy", tape_info );
    fuse_tape_info_free( tape_info ); tape_info = NULL;
    if( frames( 2, &frame_turns ) ||
        operation( "stop tape recording", fuse_host_stop_tape_recording() ) ||
        operation( "save recorded tape", fuse_host_save_tape_file( argv[3] ) ) ||
        operation( "saved tape info", fuse_host_copy_tape_info( &saved_tape_info ) ) ||
        operation( "tape eject", fuse_host_eject_tape() ) ) goto cleanup;
    print_tape_info( "saved recorded tape copy", saved_tape_info );
    puts( "host: recorded two frames; saved and ejected tape" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( tape_lifecycle ) {
    if( operation( "tape insertion", fuse_host_insert_tape_file( argv[3] ) ) ||
        operation( "tape rewind", fuse_host_rewind_tape() ) ||
        operation( "tape selection", fuse_host_select_tape_block( 0 ) ) ||
        operation( "tape play toggle", fuse_host_toggle_tape_play() ) ||
        operation( "playing tape info", fuse_host_copy_tape_info( &tape_info ) ) ) goto cleanup;
    print_tape_info( "playing tape copy", tape_info );
    fuse_tape_info_free( tape_info ); tape_info = NULL;
    if( frames( 2, &frame_turns ) ||
        operation( "tape stop", fuse_host_stop_tape() ) ||
        operation( "tape rewind", fuse_host_rewind_tape() ) ||
        operation( "tape save", fuse_host_save_tape_file( argv[4] ) ) ||
        operation( "saved tape info", fuse_host_copy_tape_info( &saved_tape_info ) ) ||
        operation( "tape eject", fuse_host_eject_tape() ) ||
        operation( "empty tape info", fuse_host_copy_tape_info( &tape_info ) ) ) goto cleanup;
    print_tape_info( "saved tape copy", saved_tape_info );
    print_tape_info( "ejected tape copy", tape_info );
    puts( "host: tape deck controls; saved and ejected" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( disk_save ) {
    if( operation( "disk insertion", fuse_host_insert_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, argv[3] ) ) ||
        operation( "disk save", fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, argv[4] ) ) ) goto cleanup;
    puts( "host: saved disk through plus3 drive 0" );
    status = EXIT_SUCCESS;
    goto cleanup; /* Core codecs handle both input and output; no CPU turn needed. */
  }
  if( disk_lifecycle ) {
    if( operation( "new disk", fuse_host_new_disk( FUSE_DISK_CONTROLLER_PLUS3, 0 ) ) ||
        operation( "drive info", fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk ) ) ) goto cleanup;
    print_disk_info( "new disk copy", disk );
    if( operation( "flip", fuse_host_set_disk_flipped( disk->controller, disk->slot, 1 ) ) ||
        operation( "write protect", fuse_host_set_disk_write_protected( disk->controller, disk->slot, 1 ) ) ) goto cleanup;
    /* The identity is a selector, not a handle: each command resolves current state. */
    fuse_disk_drive_info_free( disk ); disk = NULL;
    if( operation( "drive info", fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk ) ) ) goto cleanup;
    print_disk_info( "controls copy", disk );
    if( operation( "unflip", fuse_host_set_disk_flipped( disk->controller, disk->slot, 0 ) ) ||
        operation( "write enable", fuse_host_set_disk_write_protected( disk->controller, disk->slot, 0 ) ) ||
        operation( "disk save", fuse_host_save_disk_file( disk->controller, disk->slot, argv[3] ) ) ||
        operation( "saved drive info", fuse_host_copy_disk_drive_info( disk->controller, disk->slot, &saved_disk ) ) ||
        operation( "disk eject", fuse_host_eject_disk( disk->controller, disk->slot ) ) ) goto cleanup;
    fuse_disk_drive_info_free( disk ); disk = NULL;
    if( operation( "empty drive info", fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk ) ) ) goto cleanup;
    print_disk_info( "ejected drive copy", disk );
    puts( "host: new disk; flip/WP controls; saved and ejected plus3 drive 0" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( argc == 4 ) {
    if( operation( "File Open", fuse_host_open_file( argv[3] ) ) ) goto cleanup;
    puts( "host: File Open returned legacy success" );
    if( frames( 5, &frame_turns ) ) goto cleanup;
    status = EXIT_SUCCESS;
    goto cleanup; /* Neither dispatch success nor five turns prove loading complete. */
  }
  if( argc == 3 ) {
    if( operation( "tape insertion", fuse_host_insert_tape_file( argv[2] ) ) ) goto cleanup;
    puts( "host: inserted tape; deck stopped" );
    if( frames( 5, &frame_turns ) ) goto cleanup;
    status = EXIT_SUCCESS;
    goto cleanup; /* Insertion does not start playback or program autoload. */
  }
  /* The host drives this sequence: run, handle host events, then run again.
     Frame completion does not poll native joysticks or pump the linked UI. */
  puts( "host: driving execution and handling input between turns" );
  if( frames( 2, &frame_turns ) ) goto cleanup;

  /* These scripted events stand in for input from the host's event loop.
     Holding execution just means not calling a run entry. */
  puts( "host: holding execution; delivering Space and joystick 1 fire" );
  if( operation( "key press", fuse_host_keyboard_event( INPUT_KEY_space, 1 ) ) ||
      operation( "joystick press", fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1,
                                      FUSE_HOST_JOYSTICK_FIRE, 1 ) ) ) goto cleanup;
  puts( "host: resuming execution" );
  if( frames( 1, &frame_turns ) ) goto cleanup;
  if( operation( "focus loss", fuse_host_release_all() ) ) goto cleanup;
  puts( "host: focus lost; inputs released" );

  run = fuse_run_tstates( 997 );
  if( run != FUSE_RUN_COMPLETED ) {
    fprintf( stderr, "host: budget turn stopped (run result %d)\n", (int)run );
    goto cleanup;
  }
  budget_turns++;
  puts( "host: completed relative 997-T-state budget" );
  if( operation( "reset", fuse_host_reset( FUSE_HOST_RESET_SOFT ) ) ) goto cleanup;
  puts( "host: reset 48" );
  if( operation( "select 128", fuse_host_select_machine( "128" ) ) ) goto cleanup;
  puts( "host: selected 128" );
  if( frames( 2, &frame_turns ) ) goto cleanup;
  status = EXIT_SUCCESS;

cleanup:
  if( prepared ) fuse_shutdown(); /* Also valid after a failed mutation. */
  if( saved_disk ) print_disk_info( "retained copy after shutdown", saved_disk );
  fuse_disk_drive_info_free( saved_disk );
  fuse_disk_drive_info_free( disk );
  if( saved_tape_info ) print_tape_info( "retained tape copy after shutdown", saved_tape_info );
  fuse_tape_info_free( saved_tape_info );
  fuse_tape_info_free( tape_info );
  if( filesystem_installed ) {
    if( memory.opens != memory.closes ) {
      fputs( "host: filesystem ownership mismatch\n", stderr ); status = EXIT_FAILURE;
    } else memory_filesystem_free( &memory );
  }
  if( provider.requests != provider.releases || provider.pending ) {
    fprintf( stderr, "host: ROM ownership mismatch\n" );
    status = EXIT_FAILURE;
  }
  printf( "host: %s; frame turns=%u; budget turns=%u; ROM requests=%u; releases=%u\n",
          status == EXIT_SUCCESS ? "shutdown" : "stopped", frame_turns,
          budget_turns, provider.requests, provider.releases );
  return status;
}
