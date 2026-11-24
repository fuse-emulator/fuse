#include "host-operations.h"

struct host_test_rom_provider provider;

void filesystem_tests( void );
void filesystem_prepare( void );
void filesystem_finish( void );
void filesystem_write_oracle( void );

fuse_disk_drive_info *retained_disk_info;
fuse_tape_info *retained_tape_info;

fuse_host_operation_result
insert_tape_without_decision( const char *filename )
{
  fuse_media_decision_id request = 1;
  fuse_host_operation_result result = fuse_host_insert_tape_file( filename, &request );
  assert( !request );
  return result;
}

static void
check_tape_deck_blocked( fuse_host_operation_result expected )
{
  fuse_tape_info *info = (fuse_tape_info *)1;
  fuse_media_decision_id request = 1;
  assert( fuse_host_copy_tape_info( &info ) == expected && !info );
  assert( fuse_host_copy_tape_info( NULL ) == expected );
  assert( fuse_host_toggle_tape_play() == expected );
  assert( fuse_host_stop_tape() == expected );
  assert( fuse_host_rewind_tape() == expected );
  assert( fuse_host_select_tape_block( 0 ) == expected );
  assert( fuse_host_save_tape_file( "not-written.tzx" ) == expected );
  assert( fuse_host_request_tape_eject( &request ) == expected && !request );
  assert( fuse_host_answer_tape_eject( 1, FUSE_MEDIA_DECISION_DISCARD, NULL ) == expected );
  assert( fuse_host_start_tape_recording() == expected );
  assert( fuse_host_stop_tape_recording() == expected );
  assert( fuse_host_save_snapshot_file( "not-written.szx" ) == expected );
}

fuse_host_operation_result
eject_tape_discarding_changes( void )
{
  fuse_media_decision_id request;
  fuse_host_operation_result result = fuse_host_request_tape_eject( &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION )
    result = fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL );
  return result;
}

fuse_host_operation_result
insert_disk_file_without_decision( fuse_disk_controller controller, int slot,
                                   const char *filename )
{
  fuse_open_request request;
  fuse_host_operation_result result = fuse_host_insert_disk_file( controller, slot, filename, &request );
  assert( !request.id );
  return result;
}

fuse_host_operation_result
answer_disk_eject_without_followup( fuse_media_decision_id id,
                                    fuse_media_decision_choice choice,
                                    const char *path )
{
  fuse_open_request next;
  fuse_host_operation_result result = fuse_host_answer_disk_eject( id, choice,
                                                                   path,
                                                                   &next );
  assert( !next.id );
  return result;
}

fuse_host_operation_result
insert_disk_awaiting_eject( fuse_disk_controller controller, const char *path,
                            fuse_media_decision_id *id )
{
  fuse_open_request request;
  fuse_host_operation_result result = fuse_host_insert_disk_file( controller, 0,
                                                                  path,
                                                                  &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION )
    assert( request.question == FUSE_OPEN_SAVE_FLOPPY );
  *id = request.id;
  return result;
}

fuse_host_operation_result
new_disk_without_decision( fuse_disk_controller controller, int slot )
{
  fuse_media_decision_id request = 1;
  fuse_host_operation_result result = fuse_host_new_disk( controller, slot, &request );
  assert( !request );
  return result;
}

fuse_host_operation_result
eject_disk_without_decision( fuse_disk_controller controller, int slot )
{
  fuse_media_decision_id request = 1;
  fuse_host_operation_result result = fuse_host_eject_disk( controller, slot, &request );
  assert( !request );
  return result;
}

void
check_disk_info_blocked( fuse_host_operation_result expected )
{
  fuse_disk_drive_info dummy, *info = &dummy;
  assert( fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &info ) == expected );
  assert( !info );
}

static void
release_rom( void *context )
{
  assert( context == &provider );
  free( provider.bytes ); provider.bytes = NULL;
  provider.released++;
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  char path[4096];
  long length;
  FILE *file;
  const char *filename = name;
  assert( context == &provider );
  assert( provider.acquired == provider.released );
  provider.acquired++;
  rom->context = &provider; rom->release = release_rom;
  rom->custom = provider.custom || provider.synthetic_beta;
  check_disk_info_blocked( FUSE_HOST_OPERATION_BUSY );
  check_tape_deck_blocked( FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_machine( "48" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_release_all() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_nmi() == FUSE_HOST_OPERATION_BUSY );
  assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_flipped( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_write_protected( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_open_file( "not-opened.tap", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_BUSY );
  assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_frame() == FUSE_RUN_BUSY );
  assert( fuse_run_tstates( 1 ) == FUSE_RUN_BUSY );
  fuse_shutdown();
  if( provider.fail || ( provider.fail_name && !strcmp( name, provider.fail_name ) ) ) {
    provider.rejected++;
    return 1;
  }
  if( provider.synthetic_buttons &&
      ( !strcmp( name, "didaktik80.rom" ) || !strcmp( name, "mf1.rom" ) ) ) {
    size_t size = !strcmp( name, "mf1.rom" ) ? 0x2000 : 0x3800;
    provider.bytes = calloc( size, 1 );
    assert( provider.bytes );
    rom->data = provider.bytes;
    rom->length = size;
    rom->custom = 1;
    return 0;
  }
  /* Controller/save tests do not boot Pentagon DOS or require unbundled ROMs. */
  if( provider.synthetic_beta ) {
    if( !strcmp( name, "128p-0.rom" ) ) filename = "128-0.rom";
    else if( !strcmp( name, "128p-1.rom" ) ) filename = "128-1.rom";
    else if( !strcmp( name, "trdos.rom" ) ) filename = "48.rom";
  }
  /* Cartridge operations do not execute Interface 1 firmware. */
  if( !strcmp( name, "if1-2.rom" ) ) {
    provider.bytes = calloc( 8192, 1 );
    assert( provider.bytes );
    rom->data = provider.bytes;
    rom->length = 8192;
    return 0;
  }
  assert( snprintf( path, sizeof(path), "%s/%s", provider.directory, filename ) < sizeof(path) );
  file = fopen( path, "rb" );
  assert( file && !fseek( file, 0, SEEK_END ) );
  length = ftell( file );
  assert( length > 0 && !fseek( file, 0, SEEK_SET ) );
  provider.bytes = malloc( length );
  assert( provider.bytes );
  assert( fread( provider.bytes, 1, length, file ) == length );
  assert( !fclose( file ) );
  rom->data = provider.bytes; rom->length = length;
  return 0;
}

static void
capture_input( unsigned char values[KEYBOARD_HALFROWS + 4] )
{
  libspectrum_byte attached;
  memcpy( values, keyboard_return_values, KEYBOARD_HALFROWS );
  values[KEYBOARD_HALFROWS] = joystick_kempston_read( 0, &attached );
  values[KEYBOARD_HALFROWS + 1] = joystick_timex_read( 0, 0 );
  values[KEYBOARD_HALFROWS + 2] = joystick_timex_read( 0, 1 );
  values[KEYBOARD_HALFROWS + 3] = joystick_fuller_read( 0, &attached );
}

static void
check_released( void )
{
  unsigned char values[KEYBOARD_HALFROWS + 4];
  int row;
  capture_input( values );
  for( row = 0; row < KEYBOARD_HALFROWS; row++ ) assert( values[row] == 0xff );
  assert( values[KEYBOARD_HALFROWS] == 0 );
  assert( values[KEYBOARD_HALFROWS + 1] == 0 );
  assert( values[KEYBOARD_HALFROWS + 2] == 0 );
  assert( values[KEYBOARD_HALFROWS + 3] == 0xff );
  assert( ( readport_internal( 0xfadf ) & 3 ) == 3 );
}

static void
hold_buttons( void )
{
  assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
  assert( fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_FIRE, 1 ) == OK );
  assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_LEFT, 1 ) == OK );
  assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_RIGHT, 1 ) == OK );
}

static void
check_input( void )
{
  int key, slot, output, button, swap;
  libspectrum_byte attached, before[KEYBOARD_HALFROWS], mouse_x, mouse_y;
  libspectrum_dword clock = tstates;
  settings_current.kempston_mouse = 1;
  (void)periph_update(); /* Returns reset-needed, not an error. */
  for( key = FUSE_HOST_KEY_SPACE; key <= FUSE_HOST_KEY_SYMBOL; key++ ) {
    int valid = key == FUSE_HOST_KEY_SPACE || ( key >= '0' && key <= '9' ) ||
      ( key >= 'a' && key <= 'z' ) || key >= FUSE_HOST_KEY_ENTER;
    if( !valid ) continue;
    assert( fuse_host_release_all() == OK );
    keyboard_press( key );
    memcpy( before, keyboard_return_values, sizeof(before) );
    keyboard_release( key );
    assert( fuse_host_spectrum_key_event( key, 1 ) == OK );
    assert( !memcmp( before, keyboard_return_values, sizeof(before) ) );
    assert( fuse_host_spectrum_key_event( key, 0 ) == OK );
    check_released();
  }
  /* The same slot/button call follows each configured mapping, including NONE. */
  for( slot = FUSE_HOST_JOYSTICK_1; slot <= FUSE_HOST_JOYSTICK_KEYBOARD; slot++ )
    for( output = JOYSTICK_TYPE_NONE; output < JOYSTICK_TYPE_COUNT; output++ )
      for( button = FUSE_HOST_JOYSTICK_LEFT; button <= FUSE_HOST_JOYSTICK_FIRE; button++ ) {
        unsigned char expected[KEYBOARD_HALFROWS + 4], actual[KEYBOARD_HALFROWS + 4];
        int *mapping = slot == 0 ? &settings_current.joystick_1_output :
          slot == 1 ? &settings_current.joystick_2_output : &settings_current.joystick_keyboard_output;
        int saved = *mapping;
        *mapping = output;
        joystick_press( slot, button, 1 ); capture_input( expected );
        joystick_press( slot, button, 0 );
        assert( fuse_host_joystick_event( slot, button, 1 ) == OK );
        capture_input( actual );
        assert( !memcmp( expected, actual, sizeof(expected) ) );
        assert( fuse_host_joystick_event( slot, button, 0 ) == OK );
        *mapping = saved;
        check_released();
      }
  /* Normalized keys retain composite translation and configured keyboard-joy. */
  {
    input_key keys[] = { INPUT_KEY_a, INPUT_KEY_Escape, INPUT_KEY_Left, INPUT_KEY_Right,
                        INPUT_KEY_Shift_L, INPUT_KEY_Control_R, INPUT_KEY_F1 };
    unsigned int i;
    int saved = settings_current.joystick_keyboard_output;
    for( output = JOYSTICK_TYPE_NONE; output < JOYSTICK_TYPE_COUNT; output++ ) {
      settings_current.joystick_keyboard_output = output;
      for( i = 0; i < sizeof(keys)/sizeof(keys[0]); i++ ) {
        unsigned char expected[KEYBOARD_HALFROWS + 4], actual[KEYBOARD_HALFROWS + 4];
        input_keypress_emulation( keys[i] ); capture_input( expected );
        input_keyrelease_emulation( keys[i] );
        assert( fuse_host_keyboard_event( keys[i], 1 ) == OK );
        capture_input( actual ); assert( !memcmp( expected, actual, sizeof(expected) ) );
        assert( fuse_host_keyboard_event( keys[i], 0 ) == OK );
        assert( fuse_host_release_all() == OK );
      }
    }
    settings_current.joystick_keyboard_output = saved;
  }
  settings_current.keyboard_arrows_shifted = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_Left, 1 ) == OK );
  assert( !( keyboard_return_values[0] & 1 ) ); /* Caps Shift. */
  assert( fuse_host_keyboard_event( INPUT_KEY_Left, 0 ) == OK );
  assert( keyboard_return_values[0] & 1 );
  settings_current.keyboard_arrows_shifted = 0;
  settings_current.recreated_spectrum = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == OK );
  assert( !( keyboard_return_values[3] & 1 ) ); /* Recreated a means 1 down. */
  assert( fuse_host_keyboard_event( INPUT_KEY_b, 1 ) == OK );
  assert( keyboard_return_values[3] & 1 ); /* Recreated b means 1 up. */
  settings_current.recreated_spectrum = 0;
  assert( fuse_host_release_all() == OK );
  /* Even UI-triggering values must not invoke native actions. */
  ui_widget_level = 0; ui_mouse_grabbed = 1;
  assert( fuse_host_keyboard_event( INPUT_KEY_Escape, 1 ) == OK );
  assert( ui_mouse_grabbed == 1 && !input_dispatch_pending() );
  assert( keyboard_read( 0xf7 ) != 0xff );
  assert( fuse_host_keyboard_event( INPUT_KEY_Escape, 0 ) == OK );
  assert( fuse_host_keyboard_event( INPUT_KEY_F1, 1 ) == OK );
  ui_widget_level = -1; ui_mouse_grabbed = 0;
  keyboard_synthetic_press( KEYBOARD_SYNTHETIC_PHANTOM_TYPIST, KEYBOARD_a );
  hold_buttons();
  assert( fuse_host_release_all() == OK ); check_released();
  assert( keyboard_read( 0xfd ) != 0xff );
  keyboard_synthetic_release_all( KEYBOARD_SYNTHETIC_PHANTOM_TYPIST );
  /* Clear all interfaces, even if their mapping changed since press. */
  assert( fuse_host_joystick_event( FUSE_HOST_JOYSTICK_1, FUSE_HOST_JOYSTICK_FIRE, 1 ) == OK );
  assert( joystick_kempston_read( 0, &attached ) == 0x10 );
  settings_current.joystick_1_output = JOYSTICK_TYPE_FULLER;
  assert( fuse_host_release_all() == OK ); check_released();
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  for( swap = 0; swap < 2; swap++ ) {
    settings_current.mouse_swap_buttons = swap;
    assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_LEFT, 1 ) == OK );
    assert( ( readport_internal( 0xfadf ) & 3 ) == ( swap ? 2 : 1 ) );
    assert( fuse_host_mouse_event( FUSE_HOST_MOUSE_RIGHT, 1 ) == OK );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_release_all() == OK );
  }
  mouse_x = readport_internal( 0xfbdf ); mouse_y = readport_internal( 0xffdf );
  assert( fuse_host_mouse_motion( 7, -9 ) == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + 7 ) );
  assert( readport_internal( 0xffdf ) == (libspectrum_byte)( mouse_y + 9 ) );
  mouse_x = readport_internal( 0xfbdf ); mouse_y = readport_internal( 0xffdf );
  assert( fuse_host_mouse_motion( INT_MAX, INT_MIN ) == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + INT_MAX % 256 ) );
  assert( readport_internal( 0xffdf ) == (libspectrum_byte)( mouse_y - INT_MIN % 256 ) );
  assert( fuse_host_release_all() == OK );
  assert( readport_internal( 0xfbdf ) == (libspectrum_byte)( mouse_x + INT_MAX % 256 ) );
  assert( fuse_host_keyboard_event( -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_spectrum_key_event( -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_joystick_event( 99, 0, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_joystick_event( 0, -1, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_mouse_event( 2, 1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_reset( 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  hold_buttons();
  assert( fuse_host_select_machine( "not-a-machine" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( joystick_kempston_read( 0, &attached ) == 0x10 );
  assert( fuse_host_release_all() == OK );
  assert( tstates == clock );
}

static void
active_callback( libspectrum_dword ts, int type, void *data )
{
  (void)ts; (void)type; (void)data;
  check_disk_info_blocked( FUSE_HOST_OPERATION_BUSY );
  check_tape_deck_blocked( FUSE_HOST_OPERATION_BUSY );
  assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_nmi() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_open_file( "not-opened.tap", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
}

int
host_operations_test( int argc, char **argv )
{
  fuse_host_config *config;
  fuse_host_resources resources = { &provider, acquire_rom };
  libspectrum_dword clock;
  int callback;
  assert( argc == 3 ); provider.directory = argv[2];
  provider.custom = !strcmp( argv[1], "open-custom" );
  provider.synthetic_beta = !strcmp( argv[1], "disk-save-beta" );
  provider.synthetic_buttons = !strcmp( argv[1], "machine-buttons" ) ||
                               !strcmp( argv[1], "machine-button-fixture" );
  assert( fuse_host_release_all() == FUSE_HOST_OPERATION_NOT_READY );
  check_disk_info_blocked( FUSE_HOST_OPERATION_NOT_READY );
  check_tape_deck_blocked( FUSE_HOST_OPERATION_NOT_READY );
  assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( fuse_host_set_disk_flipped( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( fuse_host_set_disk_write_protected( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( fuse_host_open_file( "not-opened.tap", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_NOT_READY );
  config = fuse_host_config_alloc();
  if( !strcmp( argv[1], "disk-save" ) || !strcmp( argv[1], "disk-lifecycle-readback" ) )
    assert( !fuse_host_config_set_machine( config, "plus3" ) );
  if( !strcmp( argv[1], "disk-save-beta" ) ) assert( !fuse_host_config_set_machine( config, "pentagon" ) );
  assert( !fuse_host_config_set_sound( config, 0 ) );
  if( !strcmp( argv[1], "microdrive" ) || !strcmp( argv[1], "rs232" ) ||
      !strncmp( argv[1], "host-quit", 9 ) )
    assert( !fuse_host_config_set_interface1( config, 1 ) );
  assert( fuse_host_config_set_auto_load( NULL, 0 ) );
  assert( fuse_host_config_set_auto_load( config, 2 ) );
  assert( fuse_host_config_set_tape_traps( NULL, 0 ) );
  assert( !fuse_host_config_set_tape_traps( config, 1 ) );
  assert( fuse_host_config_set_tape_traps( config, -1 ) );
  assert( fuse_host_config_set_tape_traps( config, 2 ) );
  if( !strcmp( argv[1], "tape-recording" ) || !strcmp( argv[1], "legacy-tape-recording" ) ||
      !strcmp( argv[1], "tape-info" ) )
    assert( !fuse_host_config_set_tape_traps( config, 0 ) );
  if( !strncmp( argv[1], "open-", 5 ) )
    assert( !fuse_host_config_set_auto_load( config, strcmp( argv[1], "open-noauto" ) != 0 ) );
  if( !strcmp( argv[1], "filesystem" ) || !strcmp( argv[1], "filesystem-oracle" ) )
    assert( !fuse_host_config_set_auto_load( config, 0 ) );
  if( !strcmp( argv[1], "filesystem" ) ) filesystem_prepare();
  assert( fuse_host_config_set_joystick( config, 3, 0 ) );
  assert( fuse_host_config_set_joystick( config, 0, 8 ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_1, JOYSTICK_TYPE_KEMPSTON ) );
  assert( !fuse_host_config_set_joystick( config, FUSE_HOST_JOYSTICK_2, JOYSTICK_TYPE_FULLER ) );
  assert( fuse_host_config_set_mouse_swap_buttons( config, 2 ) );
  assert( !fuse_host_config_set_mouse_swap_buttons( config, 1 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  assert( settings_current.joystick_1_output == JOYSTICK_TYPE_KEMPSTON );
  assert( settings_current.joystick_2_output == JOYSTICK_TYPE_FULLER );
  assert( settings_current.mouse_swap_buttons == 1 );
  assert( settings_current.tape_traps ==
          ( strcmp( argv[1], "tape-recording" ) && strcmp( argv[1], "legacy-tape-recording" ) &&
            strcmp( argv[1], "tape-info" ) ? 1 : 0 ) );
  if( !strcmp( argv[1], "filesystem" ) ) {
    filesystem_tests();
  } else if( !strcmp( argv[1], "filesystem-oracle" ) ) {
    filesystem_write_oracle();
  } else if( !strcmp( argv[1], "tape-info" ) ) {
    check_tape_info();
  } else if( !strcmp( argv[1], "tape-deck" ) || !strcmp( argv[1], "legacy-tape-deck" ) ) {
    check_tape_deck( !strcmp( argv[1], "tape-deck" ) );
  } else if( !strcmp( argv[1], "tape-recording" ) || !strcmp( argv[1], "legacy-tape-recording" ) ) {
    check_tape_recording( !strcmp( argv[1], "tape-recording" ) );
  } else if( !strcmp( argv[1], "snapshot-roundtrip" ) ) {
    check_snapshot_roundtrip();
  } else if( !strcmp( argv[1], "trapped-recording" ) ) {
    check_trap_recording();
  } else if( !strcmp( argv[1], "recording-readback" ) ) {
    assert( insert_tape_without_decision( "recorded.pzx" ) == OK && tape_present() );
    fuse_media_decision_id request;
    assert( fuse_host_select_tape_block( 0 ) == OK );
    assert( fuse_host_request_tape_eject( &request ) == OK && !request );
  } else if( !strcmp( argv[1], "disk-fixture" ) ) {
    write_disk_fixture( "sample.udi" );
  } else if( !strcmp( argv[1], "disk-lifecycle-readback" ) ) {
    check_lifecycle_disk_file();
  } else if( !strcmp( argv[1], "disk-save" ) || !strcmp( argv[1], "disk-save-beta" ) ) {
    check_disk_save( !strcmp( argv[1], "disk-save" ) ? FUSE_DISK_CONTROLLER_PLUS3 : FUSE_DISK_CONTROLLER_BETA );
  } else if( provider.synthetic_buttons ) {
    check_machine_buttons( !strcmp( argv[1], "machine-button-fixture" ) );
  } else if( !strcmp( argv[1], "profile" ) ) {
    check_profile();
  } else if( !strcmp( argv[1], "psg-logging" ) ) {
    check_psg_logging();
  } else if( !strcmp( argv[1], "live-movie" ) ) {
    check_live_movie();
  } else if( !strcmp( argv[1], "svg-capture" ) ) {
    check_svg_capture();
  } else if( !strcmp( argv[1], "screenshots" ) ) {
    check_screenshots();
  } else if( !strcmp( argv[1], "rs232" ) ) {
    check_rs232();
  } else if( !strcmp( argv[1], "cartridge-lifecycle" ) ) {
    check_cartridge_lifecycle();
  } else if( !strcmp( argv[1], "microdrive" ) ) {
    check_microdrive();
  } else if( !strcmp( argv[1], "host-quit" ) || !strcmp( argv[1], "host-quit-shutdown" ) ) {
    check_host_quit( !strcmp( argv[1], "host-quit-shutdown" ) );
  } else if( !strcmp( argv[1], "floppy-merge" ) ) {
    check_floppy_merge();
  } else if( !strcmp( argv[1], "open-floppy" ) ) {
    check_open_floppy();
  } else if( !strcmp( argv[1], "host-poke-finder" ) ) {
    check_host_poke_finder();
  } else if( !strcmp( argv[1], "host-binary" ) ) {
    check_host_binary();
  } else if( !strcmp( argv[1], "open-pok" ) ) {
    check_pok_open();
  } else if( !strcmp( argv[1], "record-rzx-movie" ) ) {
    check_rzx_movie();
  } else if( !strcmp( argv[1], "record-rzx-rollback" ) ) {
    check_rzx_rollback();
  } else if( !strcmp( argv[1], "record-rzx-restore" ) ) {
    check_rzx_recording_restore();
  } else if( !strcmp( argv[1], "record-rzx" ) ) {
    check_rzx_recording_operations();
  } else if( !strcmp( argv[1], "rzx-joystick-fixture" ) ) {
    write_joystick_rzx( "joystick.rzx", 1 );
  } else if( !strcmp( argv[1], "open-rzx-embedded" ) || !strcmp( argv[1],
                                                                 "open-rzx-external" ) )
  {
    check_rzx_open( !strcmp( argv[1], "open-rzx-embedded" ) );
  } else if( !strcmp( argv[1], "snapshot-joystick-fixture" ) ) {
    write_snapshot_joystick_fixture();
  } else if( !strcmp( argv[1], "open-snapshot-joystick" ) ) {
    check_snapshot_joystick_decisions();
  } else if( !strncmp( argv[1], "open-", 5 ) ) {
    check_file_open( argv[1] );
  } else if( !strcmp( argv[1], "tape-contract" ) ) {
    check_tape_workflow();
  } else if( !strcmp( argv[1], "autoload-success" ) ||
             !strcmp( argv[1], "autoload-failure" ) ) {
    check_tape_autoload( !strcmp( argv[1], "autoload-failure" ) );
  } else if( strcmp( argv[1], "success" ) ) {
    if( !strncmp( argv[1], "peripheral-", 11 ) ) {
      settings_current.beta128 = 1;
      (void)periph_update();
      provider.fail_name = "trdos.rom"; /* Main model ROMs must still load. */
    } else provider.fail = 1;
    if( !strcmp( argv[1], "reset-failure" ) ||
        !strcmp( argv[1], "peripheral-reset-failure" ) )
      assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == FUSE_HOST_OPERATION_FAILED );
    else assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_FAILED );
    assert( provider.rejected == 1 );
    assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_NOT_READY );
    assert( fuse_host_nmi() == FUSE_HOST_OPERATION_NOT_READY );
  } else {
    assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
    check_input(); clock = tstates;
    hold_buttons();
    assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK );
    assert( tstates == clock && keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
    assert( keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( fuse_host_select_machine( "128" ) == OK );
    assert( keyboard_return_values[1] != 0xff );
    assert( ( readport_internal( 0xfadf ) & 3 ) == 0 );
    assert( machine_current->machine == LIBSPECTRUM_MACHINE_128 );
    assert( fuse_emulation_paused == 2 );
    assert( fuse_run_frame() == FUSE_RUN_PAUSED );
    assert( fuse_host_release_all() == OK ); check_released();
    assert( fuse_host_select_machine( "48" ) == OK );
    assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
    callback = event_register( active_callback, "Host operation reentry" );
    assert( callback >= 0 ); event_add( tstates, callback );
    assert( fuse_run_tstates( 1 ) == FUSE_RUN_COMPLETED );
    event_reset(); tstates = 0; z80.pc.w = 0x8000; z80.sp.w = 0x9000;
    writebyte_internal( 0x8000, 0 );
    assert( fuse_host_nmi() == OK );
    assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && tstates == 0 );
    event_do_events(); assert( z80.pc.w == 0x66 && z80.sp.w == 0x8ffe );
    fuse_exiting = 1;
    check_disk_info_blocked( FUSE_HOST_OPERATION_EXITING );
    check_tape_deck_blocked( FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_keyboard_event( INPUT_KEY_a, 1 ) == FUSE_HOST_OPERATION_EXITING );
    assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_EXITING );
    assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_set_disk_flipped( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_set_disk_write_protected( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_EXITING );
    assert( fuse_host_open_file( "not-opened.tap", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_EXITING );
    assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_EXITING );
  }
  assert( provider.acquired == provider.released );
  fuse_shutdown(); fuse_shutdown();
  if( !strcmp( argv[1], "filesystem" ) ) filesystem_finish();
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( !fuse_host_resources_active() );
  check_disk_info_blocked( FUSE_HOST_OPERATION_NOT_READY );
  check_tape_deck_blocked( FUSE_HOST_OPERATION_NOT_READY );
  if( retained_tape_info ) {
    assert( retained_tape_info->playing && retained_tape_info->modified && retained_tape_info->current_block == 1 );
    assert( retained_tape_info->block_count == 4 && strlen( retained_tape_info->blocks[2].details ) == 20000 );
    assert( !strcmp( retained_tape_info->blocks[0].description, "Standard Speed Data" ) );
    fuse_tape_info_free( retained_tape_info );
  }
  if( retained_disk_info ) {
    assert( retained_disk_info->loaded && !strcmp( retained_disk_info->filename, "api-copy.udi" ) );
    assert( retained_disk_info->name && retained_disk_info->enabled && retained_disk_info->available );
    fuse_disk_drive_info_free( retained_disk_info );
  }
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( fuse_host_open_file( "not-opened.tap", &(fuse_open_request){ 0 } ) == FUSE_HOST_OPERATION_NOT_READY );
  assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_NOT_READY );
  return 0;
}
