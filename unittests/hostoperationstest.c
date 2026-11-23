/* Private owner-thread operations. Each invocation owns one fresh process. */
#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "fuse-engine.h"
#include "fuse-legacy.h"
#include "pokefinder/pokemem.h"
#include "spectrum.h"
#include "event.h"
#include "display.h"
#include "keyboard.h"
#include "machine.h"
#include "memory.h"
#include "periph.h"
#include "phantom_typist.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/joystick.h"
#include "peripherals/ula.h"
#include "ui/uimedia.h"
#include "settings.h"
#include "snapshot.h"
#include "rzx.h"
#include "movie.h"
#include "tape.h"
#include "tape_internals.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"

#undef NDEBUG
#include <assert.h>

#define OK FUSE_HOST_OPERATION_OK
static struct {
  const char *directory;
  int fail, acquired, released, rejected, custom, synthetic_beta;
  const char *fail_name;
  unsigned char *bytes;
} provider;

void filesystem_tests( void );
void filesystem_prepare( void );
void filesystem_finish( void );
void filesystem_write_oracle( void );

static fuse_disk_drive_info *retained_disk_info;
static fuse_tape_info *retained_tape_info;

static fuse_host_operation_result
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

static fuse_host_operation_result
eject_tape_discarding_changes( void )
{
  fuse_media_decision_id request;
  fuse_host_operation_result result = fuse_host_request_tape_eject( &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION )
    result = fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL );
  return result;
}

static fuse_host_operation_result
insert_disk_file_without_decision( fuse_disk_controller controller, int slot,
                                   const char *filename )
{
  fuse_open_request request;
  fuse_host_operation_result result = fuse_host_insert_disk_file( controller, slot, filename, &request );
  assert( !request.id );
  return result;
}

static fuse_host_operation_result
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

static fuse_host_operation_result
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

static fuse_host_operation_result
new_disk_without_decision( fuse_disk_controller controller, int slot )
{
  fuse_media_decision_id request = 1;
  fuse_host_operation_result result = fuse_host_new_disk( controller, slot, &request );
  assert( !request );
  return result;
}

static fuse_host_operation_result
eject_disk_without_decision( fuse_disk_controller controller, int slot )
{
  fuse_media_decision_id request = 1;
  fuse_host_operation_result result = fuse_host_eject_disk( controller, slot, &request );
  assert( !request );
  return result;
}

static void
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

static int
manual_tape_toggle( void ) { return tape_toggle_play( 0 ); }

static int
deck_call( int host, fuse_host_operation_result (*entry)( void ), int (*legacy)( void ) )
{
  if( host ) {
    fuse_host_operation_result result = entry();
    assert( result == OK || result == FUSE_HOST_OPERATION_MEDIA_ERROR );
    return result != OK;
  }
  return !!legacy();
}

static int
select_deck_block( int host, int block )
{
  if( host ) {
    fuse_host_operation_result result = fuse_host_select_tape_block( block );
    assert( result == OK || result == FUSE_HOST_OPERATION_MEDIA_ERROR );
    return result != OK;
  }
  return !!tape_select_block( block );
}

static int
save_deck( int host, const char *filename )
{
  if( host ) {
    fuse_host_operation_result result = fuse_host_save_tape_file( filename );
    assert( result == OK || result == FUSE_HOST_OPERATION_MEDIA_ERROR );
    return result != OK;
  }
  return !!tape_write( filename );
}

struct deck_edges { int count; libspectrum_dword when; };
static void
count_deck_edges( gpointer data, gpointer user_data )
{
  event_t *event = data;
  struct deck_edges *edges = user_data;
  if( event->type == tape_edge_event ) { edges->count++; edges->when = event->tstates; }
}

static struct deck_edges
deck_edges( void )
{
  struct deck_edges edges = { 0, 0 };
  event_foreach( count_deck_edges, &edges ); return edges;
}

static libspectrum_error
info_error( libspectrum_error error, const char *format, va_list ap )
{
  fuse_tape_info *info = (fuse_tape_info *)1;
  (void)format; (void)ap;
  assert( fuse_host_copy_tape_info( &info ) == FUSE_HOST_OPERATION_BUSY && !info );
  assert( fuse_host_select_tape_block( 0 ) == FUSE_HOST_OPERATION_BUSY );
  return error;
}

static void
check_tape_info( void )
{
  const unsigned char bytes[] = { 2, 0, 0xff, 0xff, 2, 0, 0, 0 };
  fuse_tape_info *a, *b, *empty;
  libspectrum_tape_block *comment, *bad, *current, *active;
  libspectrum_tape_iterator iterator;
  libspectrum_error_function_t previous_error;
  struct deck_edges edges, after;
  libspectrum_dword clock;
  char *text = libspectrum_new( char, 20001 );
  FILE *file;
  size_t i;
  assert( fuse_host_copy_tape_info( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  fuse_tape_info_free( NULL );
  assert( fuse_host_copy_tape_info( &empty ) == OK );
  assert( !empty->present && !empty->playing && !empty->recording && !empty->modified );
  assert( empty->current_block == -1 && !empty->block_count && !empty->blocks );
  file = fopen( "info.tap", "wb" ); assert( file );
  assert( fwrite( bytes, 1, sizeof(bytes), file ) == sizeof(bytes) && !fclose( file ) );
  assert( insert_tape_without_decision( "info.tap" ) == OK );
  for( i = 0; i < 20000; i += 2 ) memcpy( text + i, "\xc2\xa3", 2 );
  text[20000] = 0;
  comment = libspectrum_tape_block_alloc( LIBSPECTRUM_TAPE_BLOCK_COMMENT );
  assert( !libspectrum_tape_block_set_text( comment, text ) );
  assert( !libspectrum_tape_append_block( tape, comment ) );
  bad = libspectrum_tape_block_alloc( LIBSPECTRUM_TAPE_BLOCK_GROUP_END );
  assert( !libspectrum_tape_append_block( tape, bad ) );
  tape_modified = 1;
  /* A formatter error after several copied rows frees the whole partial copy.
     Restore the controlled invalid type before normal playback/disposal. */
  assert( !libspectrum_tape_block_set_type( bad, (libspectrum_tape_type)0x7fff ) );
  previous_error = libspectrum_error_function; libspectrum_error_function = info_error;
  a = (fuse_tape_info *)1;
  assert( fuse_host_copy_tape_info( &a ) == FUSE_HOST_OPERATION_MEDIA_ERROR && !a );
  libspectrum_error_function = previous_error;
  assert( !libspectrum_tape_block_set_type( bad, LIBSPECTRUM_TAPE_BLOCK_GROUP_END ) );
  assert( fuse_host_select_tape_block( 1 ) == OK );
  assert( fuse_host_toggle_tape_play() == OK );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  active = libspectrum_tape_current_block( tape ); clock = tstates; edges = deck_edges();
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  assert( fuse_host_copy_tape_info( &a ) == OK && fuse_host_copy_tape_info( &b ) == OK );
  assert( a != b && a->blocks != b->blocks && a->block_count == 4 && b->block_count == 4 );
  assert( a->present && a->playing && !a->recording && a->modified && a->current_block == 1 );
  current = libspectrum_tape_iterator_init( &iterator, tape );
  for( i = 0; i < a->block_count; i++, current = libspectrum_tape_iterator_next( &iterator ) ) {
    char description[80], *details;
    assert( !libspectrum_tape_block_description( description, sizeof(description), current ) );
    assert( !tape_block_details( &details, current ) );
    assert( !strcmp( description, a->blocks[i].description ) && !strcmp( details, a->blocks[i].details ) );
    assert( !strcmp( a->blocks[i].description, b->blocks[i].description ) );
    assert( !strcmp( a->blocks[i].details, b->blocks[i].details ) );
    assert( a->blocks[i].description != b->blocks[i].description && a->blocks[i].details != b->blocks[i].details );
    libspectrum_free( details );
  }
  assert( strlen( a->blocks[2].details ) == 20000 && !strcmp( a->blocks[2].details, text ) );
  assert( a->blocks[2].details != text && !*a->blocks[3].details );
  b->blocks[2].details[0] = 'X'; b->present = 0;
  fuse_tape_info_free( b );
  for( i = 0; i < 8; i++ ) {
    assert( fuse_host_copy_tape_info( &b ) == OK && b->present );
    assert( !strcmp( b->blocks[2].details, text ) ); fuse_tape_info_free( b );
  }
  after = deck_edges();
  assert( after.count == edges.count && after.when == edges.when && tstates == clock );
  assert( libspectrum_tape_current_block( tape ) == active );
  assert( tape_get_current_block() == 1 && tape_modified && tape_is_playing() && fuse_emulation_paused == 2 );
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( fuse_host_select_tape_block( 0 ) == OK );
  assert( fuse_host_copy_tape_info( &b ) == OK && b->current_block == 0 );
  assert( a->current_block == 1 && a->playing && a->block_count == 4 );
  fuse_tape_info_free( b );
  /* Replacement disposes the old blocks, including long metadata, not the copy. */
  file = fopen( "replacement.tap", "wb" ); assert( file );
  assert( fwrite( bytes, 1, 4, file ) == 4 && !fclose( file ) );
  {
    char path[] = "replacement.tap";
    fuse_media_decision_id request;
    assert( fuse_host_insert_tape_file( path, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    /* The continuation owns both bytes and original name. */
    path[0] = 'x';
    assert( !remove( "replacement.tap" ) );
    assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
  }
  assert( fuse_host_copy_tape_info( &b ) == OK );
  assert( b->present && !b->playing && !b->modified && b->block_count == 1 && b->current_block == 0 );
  fuse_tape_info_free( b );
  assert( a->playing && a->modified && a->current_block == 1 && strlen( a->blocks[2].details ) == 20000 );
  assert( !empty->present && !empty->block_count && empty->current_block == -1 );
  fuse_tape_info_free( empty );
  retained_tape_info = a;
  assert( eject_tape_discarding_changes() == OK );
  assert( !remove( "info.tap" ) );
  settings_current.competition_mode = 1;
  assert( !rzx_start_recording( "info.rzx", 0 ) && rzx_recording );
  assert( fuse_host_copy_tape_info( &b ) == OK && rzx_recording );
  assert( !b->present && !b->playing && !b->recording && b->current_block == -1 );
  fuse_tape_info_free( b );
  assert( !rzx_stop_recording() && !remove( "info.rzx" ) );
  settings_current.competition_mode = 0;
  assert( fuse_host_start_tape_recording() == OK ); tstates += 37;
  assert( fuse_host_copy_tape_info( &b ) == OK );
  assert( b->recording && !b->present && !b->modified && !b->block_count && b->current_block == -1 );
  assert( tape_recording );
  fuse_tape_info_free( b );
  assert( fuse_host_stop_tape_recording() == OK );
  assert( fuse_host_copy_tape_info( &b ) == OK );
  assert( b->present && !b->recording && b->modified && b->block_count == 1 && b->current_block == 0 );
  fuse_tape_info_free( b );
  assert( eject_tape_discarding_changes() == OK );
}

static void
check_tape_deck( int host )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff, 2, 0, 0, 0 };
  libspectrum_tape_block *block;
  struct deck_edges edges, changed;
  libspectrum_dword clock = tstates;
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) );
  assert( !deck_call( host, fuse_host_rewind_tape, tape_rewind ) );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) == 1 );
  if( host ) {
    assert( fuse_host_select_tape_block( -1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
    assert( fuse_host_save_tape_file( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
    assert( fuse_host_save_tape_file( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  }
  assert( !tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP, "deck.tap", 0 ) );
  phantom_typist_activate( machine_current->machine, 0 );
  assert( phantom_typist_is_active() );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) && tape_is_playing() );
  /* Deactivation releases keys now, but the state settles at its frame tick. */
  phantom_typist_frame();
  assert( !phantom_typist_is_active() );
  edges = deck_edges(); assert( edges.count == 1 && tstates == clock );
  assert( !select_deck_block( host, 1 ) && tape_get_current_block() == 1 && tape_is_playing() );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( select_deck_block( host, 99 ) == 1 && tape_get_current_block() == 1 && tape_is_playing() );
  assert( !deck_call( host, fuse_host_rewind_tape, tape_rewind ) );
  assert( tape_get_current_block() == 0 && tape_is_playing() );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) && !tape_is_playing() );
  assert( !deck_edges().count );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  block = libspectrum_tape_current_block( tape ); tape_modified = 1;
  assert( save_deck( host, "missing-directory/deck.tzx" ) == 1 );
  assert( tape_modified && tape_is_playing() && libspectrum_tape_current_block( tape ) == block );
  edges = deck_edges(); assert( edges.count == 1 );
  assert( !save_deck( host, "deck-copy.tzx" ) && !tape_modified && tape_is_playing() );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) && !tape_is_playing() );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) && !deck_edges().count );
  tape_record_start(); assert( tape_recording );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) && tape_recording );
  tstates += 37;
  assert( save_deck( host, "missing-directory/recorded.pzx" ) == 1 );
  assert( !tape_recording && tape_modified && tape_present() );
  assert( !select_deck_block( host, 2 ) );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  assert( libspectrum_tape_block_pause_tstates( block ) == 37 );
  assert( !save_deck( host, "deck-recorded.pzx" ) && !tape_modified );
  tape_record_start(); assert( tape_recording ); tstates += 11;
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !tape_recording && !tape_present() );
  assert( !tape_open( "deck-recorded.pzx", 0 ) && tape_present() );
  assert( !select_deck_block( host, 2 ) ); /* Reopened extended tape is navigable. */
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !tape_open( "deck-copy.tzx", 0 ) );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_data_length( block ) == 2 &&
          !memcmp( libspectrum_tape_block_data( block ), bytes + 2, 2 ) );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !remove( "deck-copy.tzx" ) && !remove( "deck-recorded.pzx" ) );
}

static void
check_tape_recording( int host )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  libspectrum_tape_block *block;
  struct deck_edges edges, after;
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
  tape_record_set_tape( NULL );
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); tape_record_set_tape( tape );
  rzx_playback = 1;
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); rzx_playback = 0; rzx_recording = 1;
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); rzx_recording = 0;
  /* An initialized empty deck is a recording destination, not an error. */
  assert( !tape_present() );
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  assert( tape_recording ); rzx_playback = 1;
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  rzx_playback = 0; tstates += 31;
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
  assert( !tape_recording && tape_present() && tape_modified );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  assert( libspectrum_tape_block_pause_tstates( block ) == 31 );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP, "recording.tap", 0 ) );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) );
  event_do_events(); /* Process the immediate edge before injecting intervals. */
  edges = deck_edges(); assert( edges.count == 1 );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  tstates += 17; tape_record_edge( !ula_tape_level() ); tstates += 23;
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
  assert( !tape_recording && tape_modified && tape_is_playing() && fuse_emulation_paused == 2 );
  after = deck_edges(); assert( after.count == 1 && after.when == edges.when );
  assert( !select_deck_block( host, 2 ) );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  assert( libspectrum_tape_block_pause_tstates( block ) == 23 );
  assert( !save_deck( host, "recording-copy.pzx" ) && !tape_modified );
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
  assert( !tape_modified && tape_is_playing() ); /* Zero time is not a new hold. */
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !tape_open( "recording-copy.pzx", 0 ) && tape_present() );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !remove( "recording-copy.pzx" ) );
}

static void
check_trap_recording( void )
{
  libspectrum_tape_block *block;
  fuse_media_decision_id request;
  libspectrum_dword clock = tstates;
  assert( settings_current.tape_traps && !tape_recording && !tape_present() );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  assert( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( settings_current.tape_traps && !tape_recording && !tape_present() && !tape_modified );
  assert( tstates == clock && fuse_emulation_paused == 2 );
  /* Automatic ROM SAVE needs no manual recorder: flag/payload/parity become
     one decoded ROM block, rather than captured microphone intervals. */
  z80.af.b.h = 0xff; z80.de.w = 1; z80.ix.w = 0x8000;
  writebyte_internal( z80.ix.w, 0x42 );
  assert( !tape_save_trap() && !tape_recording && tape_present() && tape_modified );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_ROM );
  assert( libspectrum_tape_block_data_length( block ) == 3 );
  assert( !memcmp( libspectrum_tape_block_data( block ), "\xff\x42\xbd", 3 ) );
  assert( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( libspectrum_tape_current_block( tape ) == block && !tape_recording );
  assert( fuse_host_request_tape_eject( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  {
    libspectrum_dword before = tstates, frame = spectrum_get_frame_count();
    libspectrum_word pc = z80.pc.w;
    int exit_status = -1;
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_run_tstates( 1000 ) == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_legacy_execute( &exit_status ) && exit_status == -1 );
    assert( tstates == before && spectrum_get_frame_count() == frame &&
            z80.pc.w == pc && fuse_emulation_paused == 2 );
  }
  assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_SAVE, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( !tape_save_trap() && tape_modified );
  assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( tape_present() && tape_modified && fuse_emulation_paused == 2 );
  assert( fuse_host_save_tape_file( "automatic.tap" ) == OK );
  /* If settings change during pulse capture, Start refuses but Stop must
     remain usable to finalize the already-active recorder. */
  settings_current.tape_traps = 0;
  assert( fuse_host_start_tape_recording() == OK && tape_recording );
  tstates += 13; settings_current.tape_traps = 1;
  assert( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_recording && settings_current.tape_traps );
  assert( fuse_host_stop_tape_recording() == OK && !tape_recording );
  assert( fuse_emulation_paused == 2 && settings_current.tape_traps );
  assert( fuse_host_request_tape_eject( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_CANCEL, NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_run_tstates( 1000 ) == FUSE_RUN_PAUSED );
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( eject_tape_discarding_changes() == OK );
  assert( insert_tape_without_decision( "automatic.tap" ) == OK && tape_present() );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_ROM );
  assert( libspectrum_tape_block_data_length( block ) == 3 );
  assert( !memcmp( libspectrum_tape_block_data( block ), "\xff\x42\xbd", 3 ) );
  assert( fuse_host_request_tape_eject( &request ) == OK && !request );
  assert( !remove( "automatic.tap" ) );
}

static void
check_snapshot_roundtrip( void )
{
  int acquired = provider.acquired;
  z80.pc.w = 0x8000; z80.sp.w = 0x9000;
  writebyte_internal( 0x8100, 0x5a );
  assert( fuse_host_save_snapshot_file( "host-state.szx" ) == OK );
  assert( provider.acquired == acquired );
  assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
  z80.pc.w = 0; writebyte_internal( 0x8100, 0 );
  assert( fuse_host_open_file( "host-state.szx",
                               &(fuse_open_request){ 0 } ) == OK );
  assert( machine_current->machine == LIBSPECTRUM_MACHINE_48 );
  assert( z80.pc.w == 0x8000 && z80.sp.w == 0x9000 && readbyte_internal( 0x8100 ) == 0x5a );
  assert( !remove( "host-state.szx" ) );
}

static compat_file_test_hooks tape_file_io;
static int tape_file_closes;

static int64_t
tape_size_failure( compat_fd fd )
{
  (void)fd;
  return -1;
}

static int
tape_file_close( compat_fd fd )
{
  tape_file_closes++;
  return tape_file_io.close( fd );
}

static libspectrum_error
tape_error( libspectrum_error error, const char *format, va_list ap )
{
  (void)format; (void)ap;
  assert( insert_tape_without_decision( "not-opened.tap" ) == FUSE_HOST_OPERATION_BUSY );
  return error;
}

static void
check_tape_workflow( void )
{
  static const unsigned char valid[] = { 2, 0, 0xff, 0xff };
  static const unsigned char bad[] = { 2, 0, 0xff, 0xff, 5, 0, 0xff };
  char filename[] = "host-driver.tap";
  FILE *file;
  compat_file_test_hooks failing;
  libspectrum_error_function_t previous_error;
  libspectrum_tape_block *block;
  libspectrum_dword clock = tstates;
  int i, microphone, saved_auto_load = settings_current.auto_load;
  int rom_requests = provider.acquired, legacy_present;
  fuse_media_decision_id request;

  assert( insert_tape_without_decision( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( insert_tape_without_decision( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  file = fopen( filename, "wb" ); assert( file );
  assert( fwrite( valid, 1, sizeof(valid), file ) == sizeof(valid) && !fclose(file) );
  /* Compare the host boundary with the exact Media > Tape > Open mechanics.
     File > Open's preference/ROM eligibility is a different existing intent. */
  for( i = 0; i <= 1; i++ ) {
    settings_current.auto_load = i;
    assert( tape_can_autoload() == i ); /* Provider supplies standard ROMs. */
    assert( !tape_open( filename, 0 ) );
    assert( tape_present() && !tape_is_playing() && tstates == clock );
    assert( !ui_tape_close() );
    assert( insert_tape_without_decision( filename ) == OK );
    assert( tape_present() && !tape_is_playing() && tstates == clock );
    assert( settings_current.auto_load == i && provider.acquired == rom_requests );
    assert( !ui_tape_close() );
  }
  settings_current.auto_load = saved_auto_load;
  file = fopen( "host-bad.tap", "wb" ); assert( file );
  assert( fwrite( bad, 1, sizeof(bad), file ) == sizeof(bad) && !fclose(file) );
  file = fopen( "host-empty.tap", "wb" ); assert( file && !fclose(file) );
  previous_error = libspectrum_error_function;
  libspectrum_error_function = tape_error;
  assert( insert_tape_without_decision( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !tape_present() && !tape_is_playing() && tstates == clock );
  libspectrum_error_function = previous_error;
  assert( tape_open( "host-bad.tap", 0 ) );
  legacy_present = tape_present();
  assert( !ui_tape_close() );
  libspectrum_error_function = tape_error;
  assert( insert_tape_without_decision( "host-bad.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_present() == legacy_present && !tape_is_playing() && tstates == clock );
  assert( !ui_tape_close() ); /* Explicit fixture cleanup, not API rollback. */
  /* Empty TAP is valid insertion; playability is not an insertion condition. */
  assert( insert_tape_without_decision( "host-empty.tap" ) == OK );
  assert( !tape_present() && !tape_is_playing() && tstates == clock );
  compat_file_test_get_hooks( &tape_file_io ); failing = tape_file_io;
  failing.get_length = tape_size_failure; failing.close = tape_file_close;
  compat_file_test_set_hooks( &failing );
  assert( insert_tape_without_decision( filename ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  compat_file_test_set_hooks( &tape_file_io );
  assert( tape_file_closes == 1 && !tape_present() );
  libspectrum_error_function = previous_error;
  assert( fuse_host_keyboard_event( INPUT_KEY_space, 0 ) == OK );

  assert( !fuse_emulation_pause() );
  assert( insert_tape_without_decision( filename ) == OK );
  filename[0] = 'x'; /* The caller's filename is not retained. */
  assert( !remove( "host-driver.tap" ) ); /* All data is owned by the deck now. */
  assert( tape_present() && !tape_is_playing() && tstates == clock );
  block = libspectrum_tape_current_block( tape ); assert( block );
  assert( libspectrum_tape_block_data( block )[0] == 0xff );
  assert( fuse_run_frame() == FUSE_RUN_PAUSED && tstates == clock );
  assert( insert_tape_without_decision( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  tape_modified = 1;
  assert( insert_tape_without_decision( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_modified && libspectrum_tape_current_block( tape ) == block );
  tape_modified = 0;
  assert( !fuse_emulation_unpause() );
  microphone = tape_microphone;
  for( i = 0; i < 5; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !tape_is_playing() && tape_microphone == microphone );
  assert( libspectrum_tape_current_block( tape ) == block );
  /* Playback remains a separate action through the existing tape mechanism. */
  assert( !tape_do_play( 0 ) );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_COMPLETED );
  microphone = tape_microphone;
  assert( fuse_run_tstates( 2200 ) == FUSE_RUN_COMPLETED );
  assert( tape_is_playing() && tape_microphone != microphone );
  /* A failed read must not close or prompt about an existing playing tape. */
  tape_modified = 1;
  assert( insert_tape_without_decision( "missing-host.tap" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( tape_modified && tape_is_playing() );
  assert( libspectrum_tape_current_block( tape ) == block );
  file = fopen( "host-replacement.tap", "wb" ); assert( file );
  assert( fwrite( valid, 1, sizeof(valid), file ) == sizeof(valid) && !fclose(file) );
  assert( fuse_host_insert_tape_file( "host-replacement.tap", &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
  assert( tape_present() && !tape_modified && !tape_is_playing() );
  /* Ordinary clean replacement needs no decision. */
  assert( insert_tape_without_decision( "host-replacement.tap" ) == OK );
  tape_record_start(); assert( tape_recording );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_insert_tape_file( "host-replacement.tap", &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_SAVE,
                                      "host-outgoing.pzx" ) == OK );
  assert( !remove( "host-outgoing.pzx" ) );
  assert( tape_present() && !tape_recording && !tape_modified && !tape_is_playing() );
  assert( !ui_tape_close() && !remove( "host-replacement.tap" ) );
  assert( !tape_present() && !remove( "host-bad.tap" ) );
  assert( !remove( "host-empty.tap" ) );
}

static int
fail_autoload_reset( int hard_reset, libspectrum_snap *snap )
{
  assert( !hard_reset && !snap );
  return 37;
}

static void
check_tape_autoload( int fail )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  int (*reset)( int, libspectrum_snap * ) = machine_current->reset;
  int error;

  assert( !phantom_typist_is_active() );
  if( fail ) machine_current->reset = fail_autoload_reset;
  /* White-box lifecycle bracket: the legacy loader can reset the machine. */
  fuse_engine_begin_operation();
  error = tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP,
                            "autoload.tap", 1 );
  machine_current->reset = reset;
  assert( error == ( fail ? 37 : 0 ) );
  assert( phantom_typist_is_active() == !fail );
  assert( tape_present() && !tape_is_playing() ); /* No tape rollback. */
  assert( fuse_engine_end_machine_operation( error ) ==
          ( fail ? FUSE_HOST_OPERATION_FAILED : OK ) );
}

static void
write_snapshot_joystick_fixture( void )
{
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_SINCLAIR_1;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x8123;
  assert( !snapshot_write( "joystick.szx" ) );
}

extern int null_ui_confirm_joystick_calls;

static void
check_snapshot_joystick_decisions( void )
{
  fuse_open_request request, next;
  fuse_media_decision_id other;
  const fuse_host_joystick keyboard = FUSE_HOST_JOYSTICK_KEYBOARD;
  const fuse_host_joystick invalid = (fuse_host_joystick)99;
  int answered = 0;

  write_snapshot_joystick_fixture();
  settings_current.joystick_1_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  settings_current.joy_kempston = 0;
  z80.pc.w = 0x9000;
  assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
  assert( fuse_host_open_file( "joystick.szx",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "joystick.szx" ) );
  assert( fuse_host_answer_snapshot_joystick( request.id, &invalid, &next ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT && !next.id && !next.question );
  do {
    fuse_host_operation_result result;
    assert( answered < 2 );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && z80.pc.w == 0x9000 );
    assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_host_request_tape_eject( &other ) ==
            FUSE_HOST_OPERATION_BUSY && !other );
    assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR ); /* Controller unavailable on 48K. */
    assert( settings_current.joystick_keyboard_output == JOYSTICK_TYPE_NONE );
    assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
    assert( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_KEMPSTON ||
            request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 );
    assert( request.detail.joystick.inputs ==
            ( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 ?
              LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 :
              LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_1 ) );
    result = fuse_host_answer_snapshot_joystick( request.id,
                                                 request.detail.joystick.type ==
                                                 LIBSPECTRUM_JOYSTICK_SINCLAIR_1
    ? &keyboard : NULL, &next );
    assert( result == ( next.id ? FUSE_HOST_OPERATION_NEEDS_DECISION : OK ) );
    assert( request.id != next.id );
    assert( fuse_host_answer_snapshot_joystick( request.id, NULL, &request ) ==
            FUSE_HOST_OPERATION_STALE && !request.id && !request.question );
    request = next;
    answered++;
  } while( request.id );
  assert( request.question == FUSE_OPEN_NO_QUESTION );
  assert( answered == 2 && z80.pc.w == 0x8123 );
  assert( settings_current.joystick_1_output == JOYSTICK_TYPE_NONE );
  assert( settings_current.joystick_keyboard_output == JOYSTICK_TYPE_SINCLAIR_1 );
  assert( settings_current.joy_kempston );
  assert( keyboard_return_values[1] == 0xff );
  assert( !null_ui_confirm_joystick_calls );
}

static void
write_joystick_rzx( const char *filename, int embedded )
{
  libspectrum_rzx *recording = libspectrum_rzx_alloc();
  libspectrum_byte *buffer = NULL;
  size_t length = 0;
  int i;
  write_snapshot_joystick_fixture();
  writebyte_internal( 0x8123, 0x76 ); /* HALT: no port input during replay. */
  z80.iff1 = z80.iff2 = 0;
  assert( !snapshot_write( "joystick.szx" ) );
  if( embedded ) {
    libspectrum_snap *snap = libspectrum_snap_alloc();
    assert( !snapshot_copy_to( snap ) );
    assert( !libspectrum_rzx_add_snap( recording, snap, 0 ) );
  }
  libspectrum_rzx_start_input( recording, 0 );
  for( i = 0; i < 7; i++ )
    assert( !libspectrum_rzx_store_frame( recording, 16, 0, NULL ) );
  assert( !libspectrum_rzx_write( &buffer, &length, recording,
                                  LIBSPECTRUM_ID_SNAPSHOT_SZX, NULL, 0,
                                  NULL ) );
  assert( !utils_write_file( filename, buffer, length ) );
  libspectrum_free( buffer );
  assert( !libspectrum_rzx_free( recording ) );
}

static void
check_rzx_open( int embedded )
{
  fuse_open_request request;
  fuse_host_operation_result result;
  int i;

  write_joystick_rzx( "joystick.rzx", embedded );
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_open_file( "joystick.rzx",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  if( !embedded ) {
    fuse_media_decision_id id = request.id;
    assert( request.question == FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !rzx_playback );
    assert( fuse_host_answer_rzx_external_snapshot( id, NULL,
                                                    &request ) ==
            FUSE_HOST_OPERATION_CANCELLED );
    assert( !request.id && !rzx_playback );
    assert( fuse_host_answer_rzx_external_snapshot( id, "joystick.szx",
                                                    &request ) ==
            FUSE_HOST_OPERATION_STALE );
    assert( fuse_host_open_file( "joystick.rzx",
                                 &request ) ==
            FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( request.id != id && !remove( "joystick.rzx" ) );
    assert( fuse_host_answer_rzx_external_snapshot( request.id, "joystick.szx",
                                                    &request ) ==
            FUSE_HOST_OPERATION_NEEDS_DECISION );
  } else {
    assert( !remove( "joystick.rzx" ) );
  }
  assert( !remove( "joystick.szx" ) );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( request.detail.joystick.type == LIBSPECTRUM_JOYSTICK_SINCLAIR_1 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !rzx_playback &&
          z80.pc.w == 0x9000 );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
  result = fuse_host_answer_snapshot_joystick( request.id, NULL, &request );
  assert( result == OK && !request.id && rzx_playback && z80.pc.w == 0x8123 );
  for( i = 0; i < 16 && rzx_playback; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !rzx_playback && !null_ui_confirm_joystick_calls );
}

static void
check_rzx_movie( void )
{
  fuse_open_request request;
  utils_file movie;
  char target[] = "capture";
  int i;

  write_joystick_rzx( "joystick.rzx", 1 );
  settings_current.joystick_1_output = JOYSTICK_TYPE_KEMPSTON;
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  settings_current.joystick_keyboard_output = JOYSTICK_TYPE_NONE;
  assert( !utils_write_file( "capture.fmf", (const unsigned char *)"old", 3 ) );
  assert( fuse_host_record_movie_from_rzx( "joystick.rzx", target, &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  target[0] = 'x'; /* The future output path belongs to Fuse. */
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( !rzx_playback && !movie_is_active() &&
          fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( !utils_read_file( "capture.fmf", &movie ) && movie.length == 3 );
  utils_close_file( &movie );
  assert( fuse_host_answer_snapshot_joystick( request.id, NULL,
                                              &request ) == OK );
  assert( !request.id && rzx_playback && movie_is_active() );
  for( i = 0; i < 16 && rzx_playback; i++ )
    assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( !rzx_playback && !movie_is_active() );
  assert( !utils_read_file( "capture.fmf", &movie ) && movie.length > 16 );
  assert( !memcmp( movie.buffer, "FMF_V1", 6 ) );
  utils_close_file( &movie );
  assert( !remove( "capture.fmf" ) );
  assert( fuse_host_record_movie_from_rzx( "joystick.rzx",
                                           "missing-directory/out.fmf",
                                           &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_answer_snapshot_joystick( request.id, NULL, &request ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !request.id && rzx_playback && !movie_is_active() );
  assert( fuse_host_stop_rzx() == OK && !rzx_playback );
  write_joystick_rzx( "external.rzx", 0 );
  assert( fuse_host_record_movie_from_rzx( "external.rzx", "cancelled.fmf",
                                           &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT );
  assert( fuse_host_answer_rzx_external_snapshot( request.id, NULL,
                                                  &request ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( !compat_file_exists( "cancelled.fmf" ) && !movie_is_active() );
  assert( !remove( "external.rzx" ) && !remove( "joystick.rzx" ) &&
          !remove( "joystick.szx" ) );
}

static void
check_rzx_recording_operations( void )
{
  fuse_open_request request;
  assert( fuse_host_insert_rzx_snapshot() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_start_rzx_recording( "host.rzx",
                                         2 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_start_rzx_recording( "host.rzx",
                                         1 ) == OK && rzx_recording );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( fuse_host_play_rzx( "host.rzx",
                              &request ) == FUSE_HOST_OPERATION_BUSY &&
          !request.id );
  assert( fuse_host_finalise_rzx( "host.rzx" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( fuse_host_finalise_rzx( "host.rzx" ) == OK );
  assert( fuse_host_play_rzx( "host.rzx",
                              &request ) == OK && !request.id && rzx_playback );
  assert( fuse_host_stop_rzx() == OK && !rzx_playback );
  assert( !remove( "host.rzx" ) );
  assert( fuse_host_start_rzx_recording( "missing-directory/output.rzx",
                                         1 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == FUSE_HOST_OPERATION_MEDIA_ERROR &&
          !rzx_recording );
}

static void
check_rzx_rollback( void )
{
  fuse_open_request request;
  size_t *frames, count;
  const fuse_host_joystick slot = FUSE_HOST_JOYSTICK_2;

  settings_current.joystick_2_output = JOYSTICK_TYPE_SINCLAIR_1;
  z80.pc.w = 0x8123;
  assert( fuse_host_start_rzx_recording( "rollback.rzx", 1 ) == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  z80.pc.w = 0x8234;
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  z80.pc.w = 0x8345;
  assert( fuse_host_insert_rzx_snapshot() == OK );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_copy_rzx_rollback_points( &frames, &count ) == OK );
  assert( count == 3 && frames[0] == 0 && frames[1] == 1 && frames[2] == 2 );
  assert( fuse_host_rollback_rzx_to( count, &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT && !request.id );
  assert( fuse_host_rollback_rzx( &request ) == OK && z80.pc.w == 0x8345 );
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  assert( fuse_host_rollback_rzx_to( 1,
                                     &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK );
  assert( rzx_recording && z80.pc.w == 0x8345 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_select_machine( "128" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_snapshot_joystick( request.id, &slot,
                                              &request ) == OK );
  assert( !request.id && z80.pc.w == 0x8234 && rzx_recording );
  assert( frames[2] == 2 ); /* Independent of the truncated recording. */
  libspectrum_free( frames );
  assert( fuse_host_copy_rzx_rollback_points( &frames, &count ) == OK );
  assert( count == 2 && frames[1] == 1 );
  libspectrum_free( frames );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( !remove( "rollback.rzx" ) && !null_ui_confirm_joystick_calls );
}

static void
check_rzx_recording_restore( void )
{
  fuse_open_request request;
  const fuse_host_joystick joystick = FUSE_HOST_JOYSTICK_2;
  fuse_media_decision_id id;
  write_snapshot_joystick_fixture();
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_record_rzx_from_snapshot( "joystick.szx", "restore.rzx", 1,
                                              &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK && !rzx_recording );
  assert( !remove( "joystick.szx" ) );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && z80.pc.w == 0x9000 );
  assert( fuse_host_stop_rzx() == FUSE_HOST_OPERATION_BUSY );
  id = request.id;
  assert( fuse_host_answer_snapshot_joystick( id, &joystick, &request ) == OK );
  assert( !request.id && rzx_recording && z80.pc.w == 0x8123 );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK );
  settings_current.joystick_2_output = JOYSTICK_TYPE_NONE;
  z80.pc.w = 0x9000;
  assert( fuse_host_continue_rzx_recording( "restore.rzx", &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MAP_JOYSTICK && request.id != id );
  assert( fuse_host_answer_snapshot_joystick( id, NULL,
                                              &(fuse_open_request){ 0 } ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !remove( "restore.rzx" ) );
  assert( fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION &&
          !rzx_recording );
  assert( fuse_host_answer_snapshot_joystick( request.id, &joystick,
                                              &request ) == OK );
  assert( rzx_recording && !request.id );
  assert( fuse_run_frame() == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_rzx() == OK && !rzx_recording );
  assert( !remove( "restore.rzx" ) && !null_ui_confirm_joystick_calls );
}

static void
check_host_poke_finder( void )
{
  fuse_poke_match *matches;
  size_t count;

  assert( fuse_host_copy_poke_matches( &matches,
                                       &count ) == FUSE_HOST_OPERATION_STALE );
  assert( !matches && !count );
  assert( fuse_host_add_custom_pok_trainer( 9, 0,
                                            1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_add_custom_pok_trainer( 8, 0x3fff,
                                            1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_reset_poke_finder() == OK );
  assert( fuse_host_copy_poke_matches( &matches, &count ) == OK && count );
  assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK );
  assert( fuse_host_filter_poke_finder( FUSE_POKE_EXACT,
                                        0 ) == FUSE_HOST_OPERATION_STALE );
  assert( matches[0].offset < 0x4000 );
  libspectrum_free( matches );
  assert( fuse_host_reset_poke_finder() == OK );
  assert( fuse_host_filter_poke_finder( FUSE_POKE_INCREMENTED, 0 ) == OK );
  assert( fuse_host_copy_poke_matches( &matches,
                                       &count ) == OK && !matches && !count );
}

static void
check_host_binary( void )
{
  static const libspectrum_byte input[] = { 0x12, 0x34, 0, 0xff, 0x56 };
  libspectrum_byte copy[4];
  utils_file output;

  assert( !utils_write_file( "input.bin", input, sizeof( input ) ) );
  assert( fuse_host_copy_memory( 0xffff, 2,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_memory( 0x10000, 1,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_memory( 0, 0,
                                 copy ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_load_binary( "input.bin", 0xfffd,
                                 4 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_binary( "output.bin", 0xffff,
                                 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !compat_file_exists( "output.bin" ) );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_load_binary( "input.bin", 0xfffc, sizeof( copy ) ) == OK );
  assert( fuse_host_load_binary( "input.bin", 0xfffa,
                                 sizeof( input ) + 1 ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_load_binary( "missing.bin", 0xfffc,
                                 sizeof( copy ) ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_memory( 0xfffc, sizeof( copy ), copy ) == OK );
  assert( !memcmp( copy, input, sizeof( copy ) ) );
  assert( !utils_write_file( "output.bin", input, sizeof( input ) ) );
  assert( fuse_host_save_binary( "output.bin", 0xfffc, sizeof( copy ) ) == OK );
  assert( fuse_host_save_binary( "missing-directory/output.bin", 0xfffc,
                                 sizeof( copy ) ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( !utils_read_file( "output.bin", &output ) );
  assert( output.length == sizeof( copy ) && !memcmp( output.buffer, input,
                                                      output.length ) );
  utils_close_file( &output );
  assert( !remove( "input.bin" ) && !remove( "output.bin" ) );
}

extern int null_ui_pokemem_selector_calls;

static void
check_pok_open( void )
{
  static const unsigned char payload[] =
    "NFixed\nZ 8 32768 99 17\nNCustom\nZ 8 32769 256 23\nY\n";
  fuse_pok_trainers *before, *after;
  fuse_open_request request;

  writebyte_internal( 0x8000, 17 );
  writebyte_internal( 0x8001, 23 );
  assert( !utils_write_file( "trainers.pok", payload, sizeof( payload ) ) );
  assert( fuse_host_open_file( "trainers.pok",
                               &request ) == OK && !request.id );
  assert( !null_ui_pokemem_selector_calls );
  assert( fuse_host_copy_pok_trainers( &before ) == OK );
  assert( before->count == 2 && !strcmp( before->trainers[0].name, "Fixed" ) );
  assert( !before->trainers[0].active && !before->trainers[0].asks_value );
  assert( before->trainers[1].asks_value && !before->trainers[1].disabled );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 1, -1 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( readbyte_internal( 0x8001 ) == 23 );
  assert( fuse_host_set_pok_trainer( before->generation, 0, 1, -1 ) == OK );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 1, 42 ) == OK );
  assert( readbyte_internal( 0x8000 ) == 99 &&
          readbyte_internal( 0x8001 ) == 42 );
  assert( fuse_host_copy_pok_trainers( &after ) == OK );
  assert( after->trainers[0].active && after->trainers[1].active );
  assert( !before->trainers[0].active );
  fuse_pok_trainers_free( after );
  assert( fuse_host_set_pok_trainer( before->generation, 1, 0, -1 ) == OK );
  assert( readbyte_internal( 0x8001 ) == 23 );
  assert( fuse_host_open_file( "trainers.pok", &request ) == OK );
  assert( readbyte_internal( 0x8000 ) == 17 );
  assert( fuse_host_set_pok_trainer( before->generation, 0, 1, -1 ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !strcmp( before->trainers[0].name, "Fixed" ) );
  fuse_pok_trainers_free( before );
  assert( !remove( "trainers.pok" ) );
}

static void
check_file_open( const char *mode )
{
  const unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  FILE *file;
  int i, requests, error;
  int autoload = settings_current.auto_load && !provider.custom;
  int (*reset)( int, libspectrum_snap * ) = machine_current->reset;
  libspectrum_dword clock;

  assert( settings_current.auto_load == ( strcmp( mode, "open-noauto" ) != 0 ) );
  fuse_open_request request = { .id = 1, .question = FUSE_OPEN_SAVE_TAPE };
  assert( fuse_host_open_file( NULL,
                               &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !request.id && request.question == FUSE_OPEN_NO_QUESTION );
  assert( fuse_host_open_file( "",
                               &request ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  file = fopen( "open.tap", "wb" ); assert( file );
  assert( fwrite( bytes, 1, sizeof(bytes), file ) == sizeof(bytes) && !fclose(file) );
  if( !strcmp( mode, "open-reset-failure" ) ) {
    machine_current->reset = fail_autoload_reset;
    assert( fuse_host_open_file( "open.tap",
                                 &request ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR );
    machine_current->reset = reset;
    assert( tape_present() && !phantom_typist_is_active() );
    /* No execution/retry after destructive failure; caller will shut down. */
  } else if( !strcmp( mode, "open-snapshot" ) || !strcmp( mode, "open-fallback" ) ) {
    if( !strcmp( mode, "open-fallback" ) ) assert( fuse_host_select_machine( "128" ) == OK );
    settings_current.late_timings = 1; z80.pc.w = 0x8123;
    assert( !snapshot_write( "open.szx" ) );
    if( !strcmp( mode, "open-fallback" ) ) {
      assert( fuse_host_select_machine( "48" ) == OK );
      provider.fail_name = "128-0.rom";
    }
    settings_current.late_timings = 0; z80.pc.w = 0x9234;
    assert( fuse_host_spectrum_key_event( FUSE_HOST_KEY_A, 1 ) == OK );
    assert( fuse_host_open_file( "open.szx", &request ) == OK );
    assert( !request.id && request.question == FUSE_OPEN_NO_QUESTION );
    assert( z80.pc.w == 0x8123 && settings_current.late_timings == 1 );
    if( !strcmp( mode, "open-fallback" ) ) {
      assert( machine_current->machine == LIBSPECTRUM_MACHINE_48 && provider.rejected == 1 );
      assert( fuse_host_keyboard_event( INPUT_KEY_space, 0 ) == OK );
    }
    for( i = 0; i < KEYBOARD_HALFROWS; i++ ) assert( keyboard_return_values[i] == 0xff );
    assert( !remove( "open.szx" ) );
  } else if( !strcmp( mode, "open-rzx-stop" ) ) {
    assert( !rzx_start_recording( "open-recording.rzx", 0 ) && rzx_recording );
    assert( fuse_host_open_file( "missing-open.tap",
                                 &request ) ==
            FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( !rzx_recording ); /* Stop occurs before even failed identification. */
    assert( !remove( "open-recording.rzx" ) );
    assert( fuse_host_open_file( "open.tap", &request ) == OK ); /* Ordinary read error permits retry. */
  } else {
    assert( !fuse_emulation_pause() );
    for( i = 0; i < 2; i++ ) {
      if( i ) { assert( fuse_host_reset( FUSE_HOST_RESET_SOFT ) == OK ); tape_modified = 1; }
      z80.pc.w = 0x8123; clock = tstates; requests = provider.acquired;
      if( !i ) {
        fuse_engine_begin_operation();
        error = utils_open_file( "open.tap", tape_can_autoload(), NULL );
        display_refresh_all();
        fuse_engine_end_operation();
        assert( !error );
      } else {
        static const unsigned char pok[] = "NTest\nM 8 32768 99 0\nY\n";
        libspectrum_tape_block *old = libspectrum_tape_current_block( tape );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( request.id && request.question == FUSE_OPEN_SAVE_TAPE );
        assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
        assert( fuse_host_answer_tape_eject( request.id,
                                             FUSE_MEDIA_DECISION_CANCEL,
                                             NULL ) ==
                FUSE_HOST_OPERATION_CANCELLED );
        assert( tape_modified && libspectrum_tape_current_block( tape ) == old );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( fuse_host_answer_tape_eject( request.id, FUSE_MEDIA_DECISION_SAVE,
                                             "missing-directory/out.pzx" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
        assert( tape_modified && libspectrum_tape_current_block( tape ) == old );
        assert( !rzx_start_recording( "open-recording.rzx", 0 ) && rzx_recording );
        assert( fuse_host_open_file( "open.tap", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
        assert( !rzx_recording && provider.acquired == requests );
        assert( !remove( "open-recording.rzx" ) && !remove( "open.tap" ) );
        assert( !utils_write_file( "open.pok", pok, sizeof( pok ) ) );
        assert( fuse_host_answer_tape_eject( request.id,
                                             FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) == OK );
        assert( !pokemem_autoload_pokfile() );
        assert( !remove( "open.pok" ) );
        assert( !utils_write_file( "open.tap", bytes, sizeof( bytes ) ) );
      }
      assert( provider.acquired == requests + autoload );
      assert( phantom_typist_is_active() == autoload );
      assert( z80.pc.w == ( autoload ? 0 : 0x8123 ) && tstates == clock );
      assert( tape_present() && !tape_modified && !tape_is_playing() && fuse_emulation_paused == 1 );
    }
    assert( !fuse_emulation_unpause() );
  }
  assert( !remove( "open.tap" ) );
}

static void
write_disk_fixture( const char *filename )
{
  disk_t disk = { 0 };
  assert( disk_new( &disk, 1, 40, DISK_DD, DISK_UDI ) == DISK_OK );
  DISK_SET_TRACK( &disk, 0, 0 );
  disk.i = 0; disk.track[0] = 0x5a;
  assert( disk_write( &disk, filename ) == DISK_OK );
  disk_close( &disk );
}

static int open_disk_autoloads;

static int
count_open_disk_autoload( void )
{
  const ui_media_drive_info_t *drive = ui_media_drive_find( FUSE_DISK_CONTROLLER_PLUS3, 0 );
  assert( drive && drive->fdd->loaded );
  open_disk_autoloads++;
  return 0;
}

extern int null_ui_query_calls;

static void
check_floppy_merge( void )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  const ui_media_drive_info_t *drive = ui_media_drive_find( controller, 0 );
  const char *side_a = "merge Side A.udi", *side_b = "merge Side B.udi";
  fuse_open_request request;
  fuse_media_decision_id old;

  settings_set_string( &settings_current.disk_try_merge, "Always" );
  settings_current.disk_ask_merge = 1;
  assert( fuse_host_select_machine( "plus3" ) == OK );
  write_disk_fixture( side_a );
  write_disk_fixture( side_b );
  assert( fuse_host_insert_disk_file( controller, 0, side_a, &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MERGE_FLOPPY &&
          request.detail.floppy.slot == 0 );
  assert( !drive->fdd->loaded && fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_disk_merge( request.id,
                                       2 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !remove( side_a ) );
  assert( fuse_host_answer_disk_merge( request.id, 1 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 2 );
  write_disk_fixture( side_a );
  drive->fdd->disk.dirty = 1;
  assert( fuse_host_open_file( side_a,
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_SAVE_FLOPPY );
  old = request.id;
  assert( fuse_host_answer_disk_merge( old, 1 ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_disk_eject( old, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                       &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.id != old && request.question == FUSE_OPEN_MERGE_FLOPPY );
  assert( !drive->fdd->loaded &&
          fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_disk_merge( old, 1 ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_disk_merge( request.id, 0 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 1 );
  assert( !remove( side_b ) );
  assert( fuse_host_open_file( side_a,
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_MERGE_FLOPPY );
  assert( fuse_host_answer_disk_merge( request.id, 1 ) == OK );
  assert( drive->fdd->loaded && drive->fdd->disk.sides == 1 );
  assert( !null_ui_query_calls );
  assert( !ui_media_drive_insert( drive, side_a, 0 ) );
  assert( null_ui_query_calls == 1 && !remove( side_a ) );
}

static void
check_open_floppy( void )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  ui_media_drive_info_t *drive = ui_media_drive_find( controller, 0 );
  ui_media_drive_info_t *other = ui_media_drive_find( controller, 1 );
  ui_media_drive_autoload_hook_fn original_autoload = drive->autoload_hook;
  fuse_open_request request;
  int rom_requests;
  int autoload = tape_can_autoload();
  unsigned char dsk[1024] = "MV - CPCEMU Disk-File\r\nDisk-Info\r\n";
  dsk[0x30] = dsk[0x31] = 1; /* One track, one side. */
  dsk[0x33] = 3; /* 768 bytes per track, including the header. */
  memcpy( dsk + 256, "Track-Info\r\n", 12 );
  dsk[0x114] = dsk[0x11b] = 2; /* 512-byte sector. */
  dsk[0x115] = 1;
  dsk[0x11a] = 1;
  assert( !utils_write_file( "incoming.dsk", dsk, sizeof( dsk ) ) );

  assert( fuse_host_select_machine( "plus3" ) == OK );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( new_disk_without_decision( controller, 1 ) == OK );
  assert( fuse_host_select_machine( "48" ) == OK );
  drive->autoload_hook = count_open_disk_autoload;

  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.id && request.question == FUSE_OPEN_SAVE_FLOPPY );
  assert( request.detail.floppy.controller == controller &&
          request.detail.floppy.slot == 0 );
  assert( !request.detail.floppy.has_save_target );
  assert( machine_current->machine == LIBSPECTRUM_MACHINE_PLUS3 );
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && !open_disk_autoloads );
  rom_requests = provider.acquired;
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_CANCEL,
                                       NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( machine_current->machine == LIBSPECTRUM_MACHINE_PLUS3 && provider.acquired == rom_requests );
  assert( drive->fdd->loaded && drive->fdd->disk.dirty && !drive->fdd->disk.filename );
  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_SAVE,
                                       "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( drive->fdd->loaded && drive->fdd->disk.dirty && !open_disk_autoloads );
  assert( fuse_host_open_file( "incoming.dsk", &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.dsk" ) );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
  assert( provider.acquired == rom_requests && open_disk_autoloads == autoload );
  assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "incoming.dsk" ) );
  assert( other->fdd->loaded && other->fdd->disk.dirty && !other->fdd->disk.filename );
  write_disk_fixture( "named-open.udi" );
  drive->fdd->disk.dirty = 1;
  assert( fuse_host_open_file( "named-open.udi",
                               &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.question == FUSE_OPEN_SAVE_FLOPPY &&
          request.detail.floppy.has_save_target );
  assert( answer_disk_eject_without_followup( request.id, FUSE_MEDIA_DECISION_CANCEL,
                                       NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( !remove( "named-open.udi" ) );
  drive->autoload_hook = original_autoload;
}

static void
check_microdrive( void )
{
  fuse_media_decision_id id, old;
  fuse_microdrive_info *info, *saved;
  assert( fuse_host_new_microdrive( 8,
                                    &id ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_new_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 5, &id ) == OK && !id );
  assert( fuse_host_set_microdrive_write_protected( 2, 1 ) == OK );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->available && info->inserted && info->modified &&
          info->write_protected && !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_save_microdrive( 2, "incoming.mdr" ) == OK );
  assert( fuse_host_eject_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 2, &id ) == OK && !id );
  assert( fuse_host_insert_microdrive( 2, "missing.mdr", &id ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR && !id );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  old = id;
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_eject_microdrive( 5,
                                      &id ) == FUSE_HOST_OPERATION_BUSY &&
          !id );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_SAVE,
                                             NULL ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_CANCEL,
                                             NULL ) ==
          FUSE_HOST_OPERATION_CANCELLED );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION && id != old );
  assert( fuse_host_answer_microdrive_eject( old, FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) ==
          FUSE_HOST_OPERATION_STALE );
  assert( !remove( "incoming.mdr" ) );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_SAVE,
                                             "missing-directory/old.mdr" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->inserted && info->modified && !info->write_protected &&
          !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_save_microdrive( 5, "incoming.mdr" ) == OK );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.mdr" ) );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) == OK );
  assert( fuse_host_copy_microdrive_info( 2, &saved ) == OK );
  assert( saved->inserted && !saved->modified && !strcmp( saved->filename,
                                                          "incoming.mdr" ) );
  assert( fuse_host_set_microdrive_write_protected( 2, 1 ) == OK );
  assert( fuse_host_new_microdrive( 2,
                                    &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_SAVE,
                                             NULL ) == OK );
  assert( fuse_emulation_paused == 1 && !fuse_emulation_unpause() );
  assert( fuse_host_copy_microdrive_info( 2, &info ) == OK );
  assert( info->inserted && info->modified && !info->write_protected &&
          !info->filename );
  assert( !strcmp( saved->filename, "incoming.mdr" ) );
  fuse_microdrive_info_free( saved );
  fuse_microdrive_info_free( info );
  assert( fuse_host_copy_microdrive_info( 5, &info ) == OK );
  assert( info->inserted && !info->modified && !info->filename );
  fuse_microdrive_info_free( info );
  assert( fuse_host_insert_microdrive( 2, "incoming.mdr", &id ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( !remove( "incoming.mdr" ) );
  fuse_shutdown(); /* Releases the staged file without requiring a reply. */
  assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
}

static void
check_host_quit( int shutdown_pending )
{
  const fuse_disk_controller controller = FUSE_DISK_CONTROLLER_PLUS3;
  fuse_quit_request request;
  fuse_media_decision_id id, tape_id;
  ui_media_drive_info_t *a = ui_media_drive_find( controller, 0 );
  ui_media_drive_info_t *b = ui_media_drive_find( controller, 1 );
  static const unsigned char tap[] = { 2, 0, 0xff, 0xff };

  assert( fuse_host_new_microdrive( 3, &id ) == OK && !id );
  assert( fuse_host_new_microdrive( 1, &id ) == OK && !id );
  assert( fuse_host_select_machine( "plus3" ) == OK );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( new_disk_without_decision( controller, 1 ) == OK );
  assert( !utils_write_file( "quit.tap", tap, sizeof( tap ) ) );
  assert( insert_tape_without_decision( "quit.tap" ) == OK );
  assert( !remove( "quit.tap" ) );
  tape_modified = 1;
  assert( fuse_host_request_tape_eject( &id ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_BUSY && !request.id );
  assert( fuse_host_answer_tape_eject( id, FUSE_MEDIA_DECISION_CANCEL, NULL ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( !fuse_emulation_pause() );
  assert( fuse_host_request_quit( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION &&
          request.media == FUSE_QUIT_TAPE );
  tape_id = request.id;
  if( shutdown_pending ) {
    fuse_shutdown();
    assert( fuse_run_frame() == FUSE_RUN_NOT_READY );
    assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                  &request ) == FUSE_HOST_OPERATION_NOT_READY );
    return;
  }
  assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION && fuse_run_tstates( 1 ) == FUSE_RUN_NEEDS_DECISION );
  assert( fuse_host_answer_tape_eject( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_SAVE, NULL, &request ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_DISCARD, NULL, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_FLOPPY && request.controller == controller && request.slot == 0 && !tape_present() );
  id = request.id;
  assert( fuse_host_answer_quit( tape_id, FUSE_MEDIA_DECISION_CANCEL, NULL, &request ) == FUSE_HOST_OPERATION_STALE );
  assert( fuse_host_answer_quit( id, FUSE_MEDIA_DECISION_DISCARD, NULL, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_FLOPPY && request.slot == 1 && !a->fdd->loaded && b->fdd->loaded );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_CANCEL, NULL, &request ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( !request.id && b->fdd->loaded && b->fdd->disk.dirty && fuse_run_frame() == FUSE_RUN_PAUSED );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION && request.slot == 1 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
    "missing-directory/out.udi", &request ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( b->fdd->loaded && b->fdd->disk.dirty );
  assert( fuse_host_request_quit( &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION && request.slot == 1 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
                                 "quit.udi",
                                 &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_MICRODRIVE && request.slot == 1 &&
          !request.has_save_target && !b->fdd->loaded );
  assert( fuse_host_answer_microdrive_eject( request.id,
                                             FUSE_MEDIA_DECISION_DISCARD,
                                             NULL ) ==
          FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_DISCARD, NULL,
                                 &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION );
  assert( request.media == FUSE_QUIT_MICRODRIVE && request.slot == 3 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_CANCEL, NULL,
                                 &request ) == FUSE_HOST_OPERATION_CANCELLED );
  assert( fuse_host_request_quit( &request ) ==
          FUSE_HOST_OPERATION_NEEDS_DECISION &&
          request.media == FUSE_QUIT_MICRODRIVE && request.slot == 3 );
  assert( fuse_host_answer_quit( request.id, FUSE_MEDIA_DECISION_SAVE,
                                 "quit.mdr",
                                 &request ) ==
          FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN );
  assert( !remove( "quit.mdr" ) );
  assert( !request.id && fuse_run_frame() == FUSE_RUN_EXITING );
  assert( !remove( "quit.udi" ) );
  assert( !fuse_emulation_unpause() );
}

static void
check_lifecycle_disk_file( void )
{
  disk_t disk = { 0 };
  ui_media_drive_info_t *drive = ui_media_drive_find( FUSE_DISK_CONTROLLER_PLUS3, 0 );
  FILE *actual_file, *expected_file;
  int actual_byte, expected_byte;
  /* Reference the full workflow, including the +3 insertion hook's preformat.
     The chosen-target Save path already has independent chooser equivalence. */
  assert( drive && !drive->fdd->loaded );
  assert( !ui_media_drive_insert( drive, NULL, 0 ) );
  assert( !ui_media_drive_flip( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) );
  assert( !ui_media_drive_flip( FUSE_DISK_CONTROLLER_PLUS3, 0, 0 ) );
  assert( !ui_media_drive_writeprotect( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) );
  assert( !ui_media_drive_writeprotect( FUSE_DISK_CONTROLLER_PLUS3, 0, 0 ) );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, "expected-lifecycle.udi" ) == OK );
  assert( !ui_media_drive_eject( FUSE_DISK_CONTROLLER_PLUS3, 0 ) );
  assert( disk_open( &disk, "lifecycle.udi", 0, 0 ) == DISK_OK );
  assert( disk.type == DISK_UDI && disk.sides == drive->get_params()->heads &&
          disk.cylinders == drive->get_params()->cylinders );
  disk_close( &disk );
  actual_file = fopen( "lifecycle.udi", "rb" );
  expected_file = fopen( "expected-lifecycle.udi", "rb" );
  assert( actual_file && expected_file );
  do {
    actual_byte = fgetc( actual_file ); expected_byte = fgetc( expected_file );
    assert( actual_byte == expected_byte );
  } while( actual_byte != EOF );
  assert( !ferror( actual_file ) && !ferror( expected_file ) );
  assert( !fclose( actual_file ) && !fclose( expected_file ) );
  assert( !remove( "expected-lifecycle.udi" ) );
}

static int (*disk_available)( void );
static int disk_available_calls;

static int
check_disk_available( void )
{
  /* Even legacy lookup/availability runs inside host-owned admission. */
  assert( fuse_engine_check_operation() == FUSE_HOST_OPERATION_BUSY );
  check_disk_info_blocked( FUSE_HOST_OPERATION_BUSY );
  assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_flipped( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_set_disk_write_protected( FUSE_DISK_CONTROLLER_PLUS3, 0, 1 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, NULL ) == FUSE_HOST_OPERATION_BUSY );
  assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_PLUS3, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_BUSY );
  disk_available_calls++;
  return disk_available();
}

static void
check_disk_save( fuse_disk_controller controller )
{
  ui_media_drive_info_t *drive = ui_media_drive_find( controller, 0 );
  disk_t check = { 0 };
  char output[] = "api-copy.udi";
  int selected, ready, loadhead, length;
  fdd_type_t type;
  fuse_disk_drive_info *info = NULL;
  assert( drive && drive->fdd && drive->is_available );
  disk_available = drive->is_available;
  drive->is_available = check_disk_available;
  assert( fuse_host_copy_disk_drive_info( controller, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_copy_disk_drive_info( (fuse_disk_controller)99, 0, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, -1, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, 99, &info ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT && !info );
  assert( fuse_host_copy_disk_drive_info( controller, 0, &info ) == OK );
  assert( info->controller == controller && !info->slot && info->available && info->enabled && !info->loaded );
  assert( !strcmp( info->name, drive->name ) && info->name != drive->name && !info->filename );
  fuse_disk_drive_info_free( info ); info = NULL;
  assert( eject_disk_without_decision( controller, 0 ) == OK );
  assert( fuse_host_set_disk_flipped( controller, 0, 1 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_set_disk_write_protected( controller, 0, 1 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_set_disk_flipped( controller, 0, 2 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_set_disk_write_protected( controller, 0, -1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( new_disk_without_decision( (fuse_disk_controller)99, 0 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( eject_disk_without_decision( controller, 99 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  for( length = 0; length < 20; length++ ) {
    utils_file truncated;
    utils_file_init( &truncated, "short-header.udi" );
    truncated.length = length;
    truncated.buffer = libspectrum_new0( unsigned char, length ? length : 1 );
    truncated.type = LIBSPECTRUM_ID_DISK_UDI;
    assert( disk_open_loaded( &check, &truncated, 0, 0 ) == DISK_OPEN );
    disk_close( &check ); utils_file_free( &truncated );
  }
  assert( fuse_host_save_disk_file( controller, 0, "not-created.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !compat_file_exists( "not-created.udi" ) );
  assert( fuse_host_save_disk_file( (fuse_disk_controller)99, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, -1, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, 99, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_disk_file( controller, 0, "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( insert_disk_file_without_decision( controller, 0, NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( insert_disk_file_without_decision( controller, 0, "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  if( controller == FUSE_DISK_CONTROLLER_PLUS3 ) {
    assert( fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_BETA, 0, &info ) == OK );
    assert( !info->available ); fuse_disk_drive_info_free( info ); info = NULL;
    assert( insert_disk_file_without_decision( FUSE_DISK_CONTROLLER_BETA, 0, "not-opened.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_BETA, 0, "not-created.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( new_disk_without_decision( FUSE_DISK_CONTROLLER_BETA, 0 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( eject_disk_without_decision( FUSE_DISK_CONTROLLER_BETA, 0 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  }
  write_disk_fixture( "api-input.udi" );
  type = drive->fdd->type; drive->fdd->type = FDD_TYPE_NONE;
  assert( fuse_host_copy_disk_drive_info( controller, 0, &info ) == OK );
  assert( info->available && !info->enabled ); fuse_disk_drive_info_free( info ); info = NULL;
  assert( insert_disk_file_without_decision( controller, 0, "api-input.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !drive->fdd->loaded );
  drive->fdd->type = type;
  assert( insert_disk_file_without_decision( controller, 0, "api-input.udi" ) == OK );
  assert( fuse_host_save_disk_file( controller, 0, "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "api-input.udi" ) );
  assert( fuse_host_save_disk_file( controller, 0, output ) == OK );
  output[0] = 'X';
  assert( !strcmp( drive->fdd->disk.filename, "api-copy.udi" ) );
  /* Real registered controller/FDD data, with deterministic write preconditions. */
  selected = drive->fdd->selected; ready = drive->fdd->ready; loadhead = drive->fdd->loadhead;
  drive->fdd->selected = drive->fdd->ready = drive->fdd->loadhead = 1;
  DISK_SET_TRACK( &drive->fdd->disk, 0, 0 );
  drive->fdd->disk.i = 0; drive->fdd->data = 0x6b; drive->fdd->marks = 0;
  assert( fdd_write_data( drive->fdd ) == FDD_OK && drive->fdd->disk.dirty );
  drive->fdd->selected = selected; drive->fdd->ready = ready; drive->fdd->loadhead = loadhead;
  assert( fuse_host_save_disk_file( controller, 0, NULL ) == OK );
  assert( !drive->fdd->disk.dirty );
  assert( fuse_host_copy_disk_drive_info( controller, 0, &retained_disk_info ) == OK );
  assert( retained_disk_info->loaded && !strcmp( retained_disk_info->filename, "api-copy.udi" ) );
  assert( retained_disk_info->filename != drive->fdd->disk.filename );
  assert( disk_open( &check, "api-copy.udi", 0, 0 ) == DISK_OK );
  DISK_SET_TRACK( &check, 0, 0 ); assert( check.track[0] == 0x6b ); disk_close( &check );
  assert( disk_open( &check, "api-input.udi", 0, 0 ) == DISK_OK );
  DISK_SET_TRACK( &check, 0, 0 ); assert( check.track[0] == 0x5a ); disk_close( &check );
  assert( fuse_host_set_disk_flipped( controller, 0, 1 ) == OK && drive->fdd->upsidedown );
  assert( fuse_host_set_disk_flipped( controller, 0, 0 ) == OK && !drive->fdd->upsidedown );
  assert( fuse_host_set_disk_write_protected( controller, 0, 1 ) == OK );
  assert( drive->fdd->wrprot && !drive->fdd->disk.dirty );
  drive->fdd->selected = drive->fdd->ready = drive->fdd->loadhead = 1;
  DISK_SET_TRACK( &drive->fdd->disk, 0, 0 );
  drive->fdd->disk.i = 0; drive->fdd->data = 0x99;
  assert( fdd_write_data( drive->fdd ) == FDD_RDONLY );
  assert( drive->fdd->disk.track[0] == 0x6b && !drive->fdd->disk.dirty );
  drive->fdd->selected = selected; drive->fdd->ready = ready; drive->fdd->loadhead = loadhead;
  assert( fuse_host_set_disk_write_protected( controller, 0, 0 ) == OK && !drive->fdd->wrprot );
  assert( eject_disk_without_decision( controller, 0 ) == OK && !drive->fdd->loaded );
  assert( new_disk_without_decision( controller, 0 ) == OK );
  assert( drive->fdd->loaded && !drive->fdd->disk.filename && drive->fdd->disk.dirty );
  assert( drive->fdd->disk.sides == drive->get_params()->heads &&
          drive->fdd->disk.cylinders == drive->get_params()->cylinders );
  {
    fuse_media_decision_id request, other;
    fuse_media_decision_id tape_request;
    int has_second = controller == FUSE_DISK_CONTROLLER_PLUS3;
    if( has_second )
      assert( insert_disk_file_without_decision( controller, 1, "api-input.udi" ) == OK );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( fuse_host_eject_disk( controller, 0, &other ) == FUSE_HOST_OPERATION_NEEDS_DECISION && other == request );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION &&
            fuse_run_tstates( 100 ) == FUSE_RUN_NEEDS_DECISION );
    assert( fuse_host_request_tape_eject( &tape_request ) == FUSE_HOST_OPERATION_BUSY && !tape_request );
    assert( fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == FUSE_HOST_OPERATION_STALE );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_DISCARD, "unused.udi" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
    assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
    if( has_second )
      assert( fuse_host_eject_disk( controller, 1, &other ) == FUSE_HOST_OPERATION_BUSY && !other );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_CANCEL, NULL ) == FUSE_HOST_OPERATION_CANCELLED );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                         "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( fuse_host_save_disk_file( controller, 0, "api-eject.udi" ) == OK );
    assert( new_disk_without_decision( controller, 0 ) == OK );
    assert( answer_disk_eject_without_followup( request,
                                                FUSE_MEDIA_DECISION_DISCARD,
                                                NULL ) ==
            FUSE_HOST_OPERATION_STALE );
    assert( drive->fdd->loaded && drive->fdd->disk.dirty );
    assert( fuse_host_eject_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
    assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE, "api-eject.udi" ) == OK );
    assert( !drive->fdd->loaded && !remove( "api-eject.udi" ) );
    {
      char path[] = "api-staged.udi";
      libspectrum_byte *outgoing;
      assert( new_disk_without_decision( controller, 0 ) == OK );
      outgoing = drive->fdd->disk.data;
      assert( insert_disk_file_without_decision( controller, 0, "missing.udi" ) ==
              FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( drive->fdd->loaded && drive->fdd->disk.data == outgoing );
      write_disk_fixture( path );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( fuse_run_frame() == FUSE_RUN_NEEDS_DECISION );
      assert( new_disk_without_decision( controller, 0 ) == FUSE_HOST_OPERATION_BUSY );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_CANCEL, NULL ) ==
              FUSE_HOST_OPERATION_CANCELLED );
      assert( drive->fdd->loaded && drive->fdd->disk.data == outgoing );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                           "missing-directory/out.udi" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( drive->fdd->loaded && drive->fdd->disk.dirty && drive->fdd->disk.data == outgoing );
      assert( insert_disk_awaiting_eject( controller, path, &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( !remove( path ) ); path[0] = 'X';
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) == OK );
      assert( drive->fdd->loaded && !strcmp( drive->fdd->disk.filename, "api-staged.udi" ) );
      assert( new_disk_without_decision( controller, 0 ) == OK );
      assert( fuse_host_new_disk( controller, 0, &request ) == FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request, FUSE_MEDIA_DECISION_SAVE,
                                           "api-outgoing.udi" ) == OK );
      assert( !remove( "api-outgoing.udi" ) );
      assert( drive->fdd->loaded && drive->fdd->disk.dirty && !drive->fdd->disk.filename );
      assert( !utils_write_file( "api-bad.udi", (const unsigned char *)"UDI!", 4 ) );
      assert( insert_disk_awaiting_eject( controller, "api-bad.udi", &request ) ==
              FUSE_HOST_OPERATION_NEEDS_DECISION );
      assert( answer_disk_eject_without_followup( request,
                                                  FUSE_MEDIA_DECISION_DISCARD,
                                                  NULL ) ==
              FUSE_HOST_OPERATION_MEDIA_ERROR );
      assert( !drive->fdd->loaded && !remove( "api-bad.udi" ) );
    }
    if( has_second ) {
      assert( fuse_host_copy_disk_drive_info( controller, 1, &info ) == OK );
      assert( info->loaded && !strcmp( info->filename, "api-input.udi" ) );
      fuse_disk_drive_info_free( info );
      assert( eject_disk_without_decision( controller, 1 ) == OK );
    }
  }
  assert( !remove( "api-input.udi" ) && !remove( "api-copy.udi" ) );
  assert( disk_available_calls );
  drive->is_available = disk_available;
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

static void
check_cartridge_lifecycle( void )
{
  const fuse_cartridge_slot if2 = FUSE_CARTRIDGE_INTERFACE2;
  const fuse_cartridge_slot dock = FUSE_CARTRIDGE_TIMEX_DOCK;
  static const unsigned char if2_image[ 0x4000 ] = { 0x76 };
  static const unsigned char dock_image[ 9 + 0x2000 ] = { 255, 2, 0, 0, 0, 0, 0,
                                                          0, 0, 0x3c };
  fuse_cartridge_info *if2_copy, *dock_copy, *info;
  unsigned char value;

  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( !info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( fuse_host_insert_cartridge( dock,
                                      "dock.dck" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_eject_cartridge( 99 ) ==
          FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( !utils_write_file( "host.if2", if2_image, sizeof( if2_image ) ) );
  assert( fuse_host_insert_cartridge( if2, "host.if2" ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_copy_cartridge_info( if2, &if2_copy ) == OK );
  assert( if2_copy->available && if2_copy->loaded &&
          !strcmp( if2_copy->filename, "host.if2" ) );
  assert( fuse_host_insert_cartridge( if2,
                                      "missing.if2" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_reset( FUSE_HOST_RESET_HARD ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x76 );
  assert( fuse_host_eject_cartridge( if2 ) == OK );
  assert( fuse_host_copy_cartridge_info( if2, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( !remove( "host.if2" ) );

  assert( fuse_host_select_machine( "2068" ) == OK );
  assert( !utils_write_file( "host.dck", dock_image, sizeof( dock_image ) ) );
  assert( fuse_host_insert_cartridge( dock, "host.dck" ) == OK );
  assert( fuse_host_copy_memory( 0, 1, &value ) == OK && value == 0x3c );
  assert( fuse_host_copy_cartridge_info( dock, &dock_copy ) == OK );
  assert( dock_copy->available && dock_copy->loaded &&
          !strcmp( dock_copy->filename, "host.dck" ) );
  assert( !utils_write_file( "bad.dck", dock_image, 9 ) );
  assert( fuse_host_insert_cartridge( dock,
                                      "bad.dck" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( !remove( "bad.dck" ) );
  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( fuse_host_insert_cartridge( dock, "host.dck" ) == OK );
  assert( fuse_host_eject_cartridge( dock ) == OK );
  assert( fuse_host_copy_cartridge_info( dock, &info ) == OK );
  assert( info->available && !info->loaded && !info->filename );
  fuse_cartridge_info_free( info );
  assert( !remove( "host.dck" ) );
  fuse_shutdown();
  assert( !strcmp( if2_copy->filename,
                   "host.if2" ) && !strcmp( dock_copy->filename, "host.dck" ) );
  fuse_cartridge_info_free( if2_copy );
  fuse_cartridge_info_free( dock_copy );
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
  if( !strcmp( argv[1], "microdrive" ) || !strncmp( argv[1], "host-quit", 9 ) )
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
