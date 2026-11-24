/* fuse-operations.c: complete engine operations for host callers
   Copyright (c) 1999-2026 Philip Kendall and others
   Copyright (c) 2026 Fredrick Meunier

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
*/

#include "config.h"

#include <string.h>

#include "display.h"
#include "event.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "fuse-host-memory.h"
#include "input.h"
#include "keyboard.h"
#include "machine.h"
#include "peripherals/dck.h"
#include "peripherals/disk/didaktik.h"
#include "peripherals/disk/disciple.h"
#include "peripherals/if2.h"
#include "peripherals/joystick.h"
#include "peripherals/kempmouse.h"
#include "peripherals/multiface.h"
#include "settings.h"
#include "snapshot.h"
#include "tape.h"
#include "ui/ui.h"
#include "utils.h"
#include "z80/z80.h"

static int
cartridge_available( fuse_cartridge_slot slot )
{
  switch( slot ) {
  case FUSE_CARTRIDGE_INTERFACE2:
    return periph_is_active( PERIPH_TYPE_INTERFACE2 );
  case FUSE_CARTRIDGE_TIMEX_DOCK:
    return !!( machine_current->capabilities &
               LIBSPECTRUM_MACHINE_CAPABILITY_TIMEX_DOCK );
  }
  return -1;
}

static int
cartridge_loaded( fuse_cartridge_slot slot )
{
  return slot == FUSE_CARTRIDGE_INTERFACE2 ? if2_active : dck_active;
}

void
fuse_cartridge_info_free( fuse_cartridge_info *info )
{
  if( !info ) return;
  libspectrum_free( info->filename );
  libspectrum_free( info );
}

fuse_host_operation_result
fuse_host_copy_cartridge_info( fuse_cartridge_slot slot,
                               fuse_cartridge_info **info )
{
  const char *filename;
  int available;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( !info ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *info = NULL;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  available = cartridge_available( slot );
  if( available < 0 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  *info = libspectrum_new0( fuse_cartridge_info, 1 );
  (*info )->available = available;
  (*info )->loaded = available && cartridge_loaded( slot );
  if( (*info )->loaded ) {
    filename = slot ==
               FUSE_CARTRIDGE_INTERFACE2 ? if2_attachment_path() :
               settings_current.dck_file;
    if( filename ) (*info )->filename = utils_safe_strdup( filename );
  }
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
check_cartridge_operation( fuse_cartridge_slot slot )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  int available;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  available = cartridge_available( slot );
  if( available < 0 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() !=
      FUSE_DECISION_NONE ) return FUSE_HOST_OPERATION_BUSY;
  return available ? FUSE_HOST_OPERATION_OK : FUSE_HOST_OPERATION_MEDIA_ERROR;
}

fuse_host_operation_result
fuse_host_insert_cartridge( fuse_cartridge_slot slot, const char *filename )
{
  fuse_host_operation_result result = check_cartridge_operation( slot );
  utils_file file;
  int error;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  utils_file_init( &file, filename );
  error = utils_file_read( &file );
  if( !error && slot == FUSE_CARTRIDGE_INTERFACE2 && file.length != 0x4000 ) {
    ui_error( UI_ERROR_ERROR, "Interface 2 cartridge must be 16384 bytes" );
    error = 1;
  }
  if( error ) {
    utils_file_free( &file );
    fuse_emulation_unpause();
    fuse_engine_end_operation();
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  }
  error = slot ==
          FUSE_CARTRIDGE_INTERFACE2 ? if2_insert_loaded( &file ) :
          dck_insert_loaded( &file );
  utils_file_free( &file );
  fuse_emulation_unpause();
  result = fuse_engine_end_machine_operation( error );
  if( result == FUSE_HOST_OPERATION_OK && !cartridge_loaded( slot ) )
    return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return result;
}

fuse_host_operation_result
fuse_host_eject_cartridge( fuse_cartridge_slot slot )
{
  fuse_host_operation_result result = check_cartridge_operation( slot );
  int error;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  fuse_engine_begin_operation();
  error = slot == FUSE_CARTRIDGE_INTERFACE2 ? if2_eject() : dck_eject();
  return fuse_engine_end_machine_operation( error );
}

static void
release_physical_input( void )
{
  int button;
  input_cancel_physical();
  for( button = 0; button < 8; button++ )
    kempmouse_update( 0, 0, button, 0 );
}

fuse_host_operation_result
fuse_host_select_machine( const char *id )
{
  int i, error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !id ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( snapshot_rzx_rollback_pending() ) return FUSE_HOST_OPERATION_BUSY;
  for( i = 0; i < machine_count; i++ )
    if( !strcmp( id, machine_types[i]->id ) ) break;
  if( i == machine_count ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  /* ID selection avoids the legacy enum/snapshot fallback and abort policy. */
  error = machine_select_id( machine_types[i]->id );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_reset( fuse_host_reset_kind kind )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( kind != FUSE_HOST_RESET_SOFT && kind != FUSE_HOST_RESET_HARD )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  error = machine_reset( kind == FUSE_HOST_RESET_HARD );
  return fuse_engine_end_machine_operation( error );
}

fuse_host_operation_result
fuse_host_open_file( const char *filename, fuse_open_request *request )
{
  int error;
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = (fuse_open_request){ 0 };
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  fuse_engine_begin_operation();
  error = utils_open_file_with_decision( filename, tape_can_autoload(), request );
  if( !request->id ) display_refresh_all();
  fuse_engine_end_operation();
  if( error ) return FUSE_HOST_OPERATION_MEDIA_ERROR;
  return request->id ? FUSE_HOST_OPERATION_NEEDS_DECISION :
         FUSE_HOST_OPERATION_OK;
}

static fuse_quit_request pending_quit;

int
fuse_host_quit_pending( void )
{
  return pending_quit.id != 0;
}

void
fuse_host_clear_quit( void )
{
  pending_quit = (fuse_quit_request){ 0 };
}

static fuse_host_operation_result
continue_quit( fuse_quit_request *next, int check_tape )
{
  fuse_host_operation_result result = FUSE_HOST_OPERATION_OK;

  if( check_tape ) result = fuse_host_request_tape_eject( &next->id );
  if( result == FUSE_HOST_OPERATION_OK ) result = fuse_disk_quit_next( next );
  if( result ==
      FUSE_HOST_OPERATION_OK ) result = fuse_microdrive_quit_next( next );
  if( result ==
      FUSE_HOST_OPERATION_OK ) result = fuse_mass_storage_quit_next( next );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) pending_quit = *next;
  if( result != FUSE_HOST_OPERATION_OK ) return result;

  fuse_exiting = 1;
  return FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN;
}

fuse_host_operation_result
fuse_host_request_quit( fuse_quit_request *request )
{
  fuse_host_operation_result result;
  if( !request ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *request = (fuse_quit_request){ 0 };
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;
  return continue_quit( request, 1 );
}

fuse_host_operation_result
fuse_host_answer_quit( fuse_media_decision_id id, fuse_media_decision_choice choice,
                       const char *path, fuse_quit_request *next )
{
  fuse_quit_request current = pending_quit;
  fuse_host_operation_result result;

  if( !next ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *next = (fuse_quit_request){ 0 };
  if( !id || id != current.id ) {
    result = fuse_engine_check_operation();
    return result == FUSE_HOST_OPERATION_OK || current.id ?
      FUSE_HOST_OPERATION_STALE : result;
  }

  if( current.media == FUSE_QUIT_MASS_STORAGE && path )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  /* The existing guarded media entry owns this synchronous step. */
  fuse_host_clear_quit();
  switch( current.media ) {
  case FUSE_QUIT_MASS_STORAGE:
    result = fuse_host_answer_mass_storage_eject( id, choice );
    break;
  case FUSE_QUIT_MICRODRIVE:
    result = fuse_host_answer_microdrive_eject( id, choice, path );
    break;
  case FUSE_QUIT_FLOPPY:
    result = fuse_host_answer_disk_eject( id, choice, path,
                                          &(fuse_open_request){ 0 } );
    break;
  default:
    result = fuse_host_answer_tape_eject( id, choice, path );
    break;
  }
  if( result == FUSE_HOST_OPERATION_INVALID_ARGUMENT || result == FUSE_HOST_OPERATION_BUSY )
    pending_quit = current;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  return continue_quit( next, 0 );
}

static int
valid_memory_range( size_t start, size_t length )
{
  return start < 0x10000 && length && length <= 0x10000 - start;
}

fuse_host_operation_result
fuse_host_copy_memory( size_t start, size_t length, libspectrum_byte *buffer )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !buffer || !valid_memory_range( start, length ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  utils_copy_memory( start, length, buffer );
  fuse_engine_end_operation();
  return FUSE_HOST_OPERATION_OK;
}

static fuse_host_operation_result
host_binary_file( const char *filename, size_t start, size_t length, int load )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename || !valid_memory_range( start, length ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( fuse_engine_pending_decision() != FUSE_DECISION_NONE )
    return FUSE_HOST_OPERATION_BUSY;

  fuse_engine_begin_operation();
  fuse_emulation_pause();
  if( load ) {
    utils_file file;
    error = utils_read_file( filename, &file );
    if( !error && file.length < length ) error = 1;
    if( !error ) utils_load_binary( start, length, file.buffer );
    utils_close_file( &file );
  } else {
    error = utils_save_binary( start, length, filename );
  }
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_load_binary( const char *filename, size_t start, size_t length )
{
  return host_binary_file( filename, start, length, 1 );
}

fuse_host_operation_result
fuse_host_save_binary( const char *filename, size_t start, size_t length )
{
  return host_binary_file( filename, start, length, 0 );
}

fuse_host_operation_result
fuse_host_save_snapshot_file( const char *filename )
{
  int error;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !filename || !*filename ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  fuse_engine_begin_operation();
  /* Chosen-target portion of menu_file_savesnapshot; no second chooser or
     overwrite decision. Preserve its pause/unpause side effects and ordering. */
  fuse_emulation_pause();
  error = snapshot_write( filename );
  fuse_emulation_unpause();
  fuse_engine_end_operation();
  return error ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_machine_button_available( fuse_machine_button button, int *available )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( !available ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *available = 0;
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  switch( button ) {
  case FUSE_MACHINE_BUTTON_MULTIFACE_RED:
    *available = multiface_red_button_available();
    break;
  case FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC:
    *available = !!disciple_available;
    break;
  case FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP:
    *available = !!didaktik80_available;
    break;
  default:
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  }
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_press_machine_button( fuse_machine_button button )
{
  int available;
  fuse_host_operation_result result =
    fuse_host_machine_button_available( button, &available );
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !available ) return FUSE_HOST_OPERATION_FAILED;
  switch( button ) {
  case FUSE_MACHINE_BUTTON_MULTIFACE_RED: multiface_red_button(); break;
  case FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC: disciple_snapshot_nmi(); break;
  case FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP: didaktik80_snap_button(); break;
  }
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_nmi( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  event_add( 0, z80_nmi_event );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_keyboard_event( input_key key, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( pressed != 0 && pressed != 1 ) ||
      !( key == INPUT_KEY_Tab || key == INPUT_KEY_Return ||
         key == INPUT_KEY_Escape ||
         ( key >= INPUT_KEY_space && key <= INPUT_KEY_BackSpace ) ||
         key == INPUT_KEY_KP_Enter ||
         ( key >= INPUT_KEY_Up && key <= INPUT_KEY_F12 ) ||
         ( key >= INPUT_KEY_Shift_L && key <= INPUT_KEY_Mode_switch ) ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) input_keypress_emulation( key );
  else input_keyrelease_emulation( key );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_spectrum_key_event( fuse_host_key key, int pressed )
{
  keyboard_key_name target;
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( pressed != 0 &&
      pressed != 1 ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( key == FUSE_HOST_KEY_ENTER ) target = KEYBOARD_Enter;
  else if( key == FUSE_HOST_KEY_CAPS ) target = KEYBOARD_Caps;
  else if( key == FUSE_HOST_KEY_SYMBOL ) target = KEYBOARD_Symbol;
  else if( key == FUSE_HOST_KEY_SPACE ||
           ( key >= FUSE_HOST_KEY_0 && key <= FUSE_HOST_KEY_9 ) ||
           ( key >= FUSE_HOST_KEY_A && key <= FUSE_HOST_KEY_Z ) )
    target = (keyboard_key_name)key;
  else return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  if( pressed ) keyboard_press( target ); else keyboard_release( target );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_joystick_event( fuse_host_joystick joystick,
                          fuse_host_joystick_button button, int pressed )
{
  static const joystick_button buttons[] = {
    JOYSTICK_BUTTON_LEFT, JOYSTICK_BUTTON_RIGHT, JOYSTICK_BUTTON_UP,
    JOYSTICK_BUTTON_DOWN, JOYSTICK_BUTTON_FIRE
  };
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( joystick < FUSE_HOST_JOYSTICK_1 ||
      joystick > FUSE_HOST_JOYSTICK_KEYBOARD ||
      button < FUSE_HOST_JOYSTICK_LEFT || button > FUSE_HOST_JOYSTICK_FIRE ||
      ( pressed != 0 &&
        pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  joystick_press( joystick == FUSE_HOST_JOYSTICK_KEYBOARD ? JOYSTICK_KEYBOARD :
                  joystick == FUSE_HOST_JOYSTICK_1 ? 0 : 1, buttons[button],
                  pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_motion( int dx, int dy )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  /* Reduce before arithmetic: even INT_MIN/MAX cannot overflow the addition. */
  kempmouse_update( dx % 256, dy % 256, -1, 0 );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_mouse_event( fuse_host_mouse_button button, int pressed )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( ( button != FUSE_HOST_MOUSE_LEFT && button != FUSE_HOST_MOUSE_RIGHT ) ||
      ( pressed != 0 &&
        pressed != 1 ) ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  input_mouse_button_event( button == FUSE_HOST_MOUSE_LEFT ? INPUT_MOUSE_LEFT :
                            INPUT_MOUSE_RIGHT, pressed );
  return FUSE_HOST_OPERATION_OK;
}

fuse_host_operation_result
fuse_host_release_all( void )
{
  fuse_host_operation_result result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  release_physical_input();
  return FUSE_HOST_OPERATION_OK;
}
