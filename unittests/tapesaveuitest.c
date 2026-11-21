/* tapesaveuitest.c: tape save error and close-decision regression tests
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
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "tape.h"
#include "tape_internals.h"
#include "ui/ui.h"
#include "utils.h"

static unsigned char rom_bytes[0x4000];
static int write_result, cancel_filename, writes, filenames, confirmations;
static int expected_pause, host_replacing;

static void
check_deck_busy( void )
{
  fuse_tape_info *info = (fuse_tape_info *)1;
  fuse_tape_eject_id request = 1;
  if( !host_replacing ) return;
  assert( fuse_host_copy_tape_info( &info ) == FUSE_HOST_OPERATION_BUSY && !info );
  assert( fuse_host_toggle_tape_play() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_stop_tape() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_rewind_tape() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_select_tape_block( 0 ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_save_tape_file( "not-written.tzx" ) == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_request_tape_eject( &request ) == FUSE_HOST_OPERATION_BUSY &&
          !request );
  assert( fuse_host_answer_tape_eject( 1, FUSE_TAPE_EJECT_DISCARD, NULL ) ==
          FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_start_tape_recording() == FUSE_HOST_OPERATION_BUSY );
  assert( fuse_host_stop_tape_recording() == FUSE_HOST_OPERATION_BUSY );
}

static fuse_host_operation_result
host_replace( void )
{
  fuse_host_operation_result result;
  host_replacing = 1;
  result = fuse_host_open_file( "replacement.tap" );
  host_replacing = 0;
  return result;
}
static const char *output_filename = "tape-save.tzx";
static ui_confirm_save_t choice = UI_CONFIRM_SAVE_SAVE;

int test_tape_save_write( const char *filename );
fuse_host_operation_result test_tape_resolve_eject(
  fuse_tape_eject_id request, fuse_tape_eject_choice choice,
  const char *filename, int *write_error );
int test_tape_finish_eject( void );
char *test_tape_save_filename( const char *title );
ui_confirm_save_t test_tape_save_confirm( const char *message );

/* Only the common UI translation unit uses these substitutions. Success uses
   the real writer; failure returns a distinctive code without writing a file. */
int
test_tape_save_write( const char *filename )
{
  assert( fuse_emulation_paused == expected_pause );
  check_deck_busy();
  writes++;
  return write_result < 0 ? tape_write( filename ) : write_result;
}

fuse_host_operation_result
test_tape_resolve_eject( fuse_tape_eject_id request, fuse_tape_eject_choice choice,
                        const char *filename, int *write_error )
{
  if( choice == FUSE_TAPE_EJECT_SAVE ) {
    assert( fuse_emulation_paused == expected_pause );
    check_deck_busy();
    writes++;
  }
  return tape_resolve_eject( request, choice, filename, write_error );
}

int
test_tape_finish_eject( void )
{
  assert( fuse_emulation_paused == expected_pause - 1 );
  check_deck_busy();
  return tape_finish_eject();
}

char *
test_tape_save_filename( const char *title )
{
  char *filename;
  (void)title;
  assert( fuse_emulation_paused == expected_pause );
  check_deck_busy();
  filenames++;
  if( cancel_filename ) return NULL;
  filename = libspectrum_new( char, strlen(output_filename) + 1 );
  strcpy( filename, output_filename );
  return filename;
}

ui_confirm_save_t
test_tape_save_confirm( const char *message )
{
  assert( message );
  check_deck_busy();
  confirmations++;
  return choice;
}

static int
acquire_rom( void *context, const char *name, fuse_host_rom *rom )
{
  (void)context;
  assert( !strcmp( name, "48.rom" ) );
  rom->data = rom_bytes; rom->length = sizeof(rom_bytes); rom->custom = 0;
  return 0;
}

static void
reset_counts( void )
{
  writes = filenames = confirmations = 0;
}

static void
check_retained( libspectrum_tape_block *block )
{
  assert( tape_present() && tape_modified && tape_is_playing() );
  assert( libspectrum_tape_current_block( tape ) == block );
  assert( fuse_emulation_paused == expected_pause - 1 );
}

int
main( int argc, char **argv )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  fuse_host_config *config;
  fuse_host_resources resources = { NULL, acquire_rom };
  libspectrum_tape_block *block;
  FILE *file;
  assert( argc == 2 );
  file = fopen( argv[1], "rb" ); assert( file );
  assert( fread( rom_bytes, 1, sizeof(rom_bytes), file ) == sizeof(rom_bytes) );
  assert( !fclose( file ) );
  config = fuse_host_config_alloc();
  assert( !fuse_host_config_set_machine( config, "48" ) );
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  assert( !tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP,
                            "input.tap", 0 ) );
  tape_modified = 1;
  assert( !tape_do_play( 0 ) );
  block = libspectrum_tape_current_block( tape ); assert( block );
  assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  expected_pause = fuse_emulation_paused + 1;

  write_result = 37;
  assert( ui_tape_write() == 37 );
  assert( writes == 1 && filenames == 1 && confirmations == 0 );
  check_retained( block );
  assert( !compat_file_exists( "tape-save.tzx" ) );

  reset_counts();
  output_filename = "missing-directory/tape-save.tzx";
  assert( ui_tape_close() == 1 ); /* Real write failure must not become discard. */
  assert( writes == 1 && filenames == 1 && confirmations == 1 );
  check_retained( block );
  assert( !utils_write_file( "replacement.tap", bytes, sizeof( bytes ) ) );
  reset_counts();
  assert( host_replace() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( writes == 1 && filenames == 1 && confirmations == 1 );
  check_retained( block );
  assert( !remove( "replacement.tap" ) );
  output_filename = "tape-save.tzx";

  reset_counts(); cancel_filename = 1;
  assert( ui_tape_close() == 1 ); /* Cancel destination. */
  assert( writes == 0 && filenames == 1 && confirmations == 1 );
  check_retained( block );

  reset_counts(); cancel_filename = 0; choice = UI_CONFIRM_SAVE_CANCEL;
  assert( ui_tape_close() == 1 );
  assert( writes == 0 && filenames == 0 && confirmations == 1 );
  check_retained( block );

  reset_counts(); choice = UI_CONFIRM_SAVE_SAVE; write_result = -1;
  assert( !ui_tape_close() ); /* Save succeeds before removal. */
  assert( writes == 1 && filenames == 1 && confirmations == 1 );
  assert( !tape_present() && !tape_modified && !tape_is_playing() );
  assert( fuse_emulation_paused == expected_pause - 1 );
  assert( !tape_open( "tape-save.tzx", 0 ) && tape_present() );
  block = libspectrum_tape_current_block( tape ); assert( block );
  assert( libspectrum_tape_block_data_length( block ) == 2 );
  assert( !memcmp( libspectrum_tape_block_data( block ), bytes + 2, 2 ) );
  assert( !remove( "tape-save.tzx" ) );

  reset_counts(); choice = UI_CONFIRM_SAVE_DONTSAVE; tape_modified = 1;
  assert( !ui_tape_close() );
  assert( writes == 0 && filenames == 0 && confirmations == 1 );
  assert( !tape_present() && !tape_modified );
  assert( fuse_emulation_paused == expected_pause - 1 );
  assert( !tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP, "recording.tap", 0 ) );
  tape_record_start(); assert( tape_recording ); tstates += 37;
  reset_counts(); choice = UI_CONFIRM_SAVE_CANCEL;
  assert( ui_tape_close() == 1 );
  assert( !tape_recording && tape_present() && tape_modified );
  assert( confirmations == 1 && !writes && !filenames );
  reset_counts(); choice = UI_CONFIRM_SAVE_DONTSAVE;
  assert( !ui_tape_close() && !tape_present() );
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  fuse_shutdown(); fuse_shutdown();
  puts( "Tape Save error propagation, retention, choices and pause balance passed" );
  return 0;
}
