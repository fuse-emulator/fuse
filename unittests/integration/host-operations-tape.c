/* host-operations-tape.c: host operation integration tests: tape
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

#include "host-operations.h"
#include "memory-filesystem.h"
#include "fuse-host-settings.h"
#include "fuse-host-debugger.h"

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

void
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

static memory_filesystem tape_filesystem;

void
tape_memory_prepare( void )
{
  assert( !memory_filesystem_install( &tape_filesystem ) );
}

void
tape_memory_finish( void )
{
  memory_file *file;
  assert( tape_filesystem.opens == tape_filesystem.closes );
  assert( tape_filesystem.reads && tape_filesystem.writes );
  for( file = tape_filesystem.files; file; file = file->next )
    assert( !file->handles );
  memory_filesystem_free( &tape_filesystem );
}

static void
check_navigation_state( int present, int block, int playing )
{
  fuse_tape_info *info;
  assert( fuse_host_copy_tape_info( &info ) == OK );
  assert( info->present == present && info->current_block == block );
  assert( info->playing == playing && !info->recording && !info->modified );
  assert( info->block_count == ( present ? 2 : 0 ) );
  fuse_tape_info_free( info );
}

/* User-visible half of tape_select_internal_unittest. Seed media in RAM,
   never replace the engine's tape global or inspect its playback flags. */
void
check_tape_navigation( void )
{
  static const unsigned char fixture[] = {
    'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1a, 1, 20,
    0x10, 0xe8, 0x03, 2, 0, 0x80, 0x80, /* ROM, 1000 ms pause */
    0x20, 1, 0                           /* 1 ms pause block */
  };
  memory_file *exported;
  void *handle;
  size_t written;
  libspectrum_tape *read_back;
  libspectrum_tape_block *block;
  libspectrum_tape_iterator iterator;

  assert( fuse_host_select_machine( "48" ) == OK );
  assert( eject_tape_discarding_changes() == OK );
  check_navigation_state( 0, -1, 0 );
  assert( fuse_host_rewind_tape() == OK );
  assert( fuse_host_select_tape_block( 0 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  check_navigation_state( 0, -1, 0 );

  handle = memory_open( "navigation.tzx", COMPAT_FILE_WRITE ); assert( handle );
  assert( !memory_write( handle, fixture, sizeof( fixture ), &written ) );
  assert( written == sizeof( fixture ) && !memory_close( handle ) );
  /* Also exercise the host's complete-read/write loops. */
  tape_filesystem.chunk = 3;
  assert( insert_tape_without_decision( "navigation.tzx" ) == OK );
  check_navigation_state( 1, 0, 0 );
  assert( fuse_host_select_tape_block( 1 ) == OK );
  check_navigation_state( 1, 1, 0 );
  assert( fuse_host_select_tape_block( 5 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  check_navigation_state( 1, 1, 0 );
  assert( fuse_host_rewind_tape() == OK );
  check_navigation_state( 1, 0, 0 );
  assert( fuse_host_toggle_tape_play() == OK );
  check_navigation_state( 1, 0, 1 );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_tape() == OK );
  check_navigation_state( 1, 0, 0 );

  /* No extension: the default export format must still be TZX. */
  assert( fuse_host_save_tape_file( "navigation-copy" ) == OK );
  check_navigation_state( 1, 0, 0 );
  exported = memory_find( "navigation-copy" ); assert( exported );
  assert( !exported->handles && exported->length >= 8 );
  assert( !memcmp( exported->data, "ZXTape!\x1a", 8 ) );
  read_back = libspectrum_tape_alloc(); assert( read_back );
  assert( !libspectrum_tape_read( read_back, exported->data, exported->length,
                                  LIBSPECTRUM_ID_UNKNOWN, NULL ) );
  assert( libspectrum_tape_count( read_back ) == 2 );
  block = libspectrum_tape_iterator_init( &iterator, read_back );
  assert( block && libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_ROM );
  assert( libspectrum_tape_block_data_length( block ) == 2 );
  assert( !memcmp( libspectrum_tape_block_data( block ), fixture + 15, 2 ) );
  block = libspectrum_tape_iterator_next( &iterator );
  assert( block && libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  libspectrum_tape_free( read_back );

  assert( eject_tape_discarding_changes() == OK );
  assert( insert_tape_without_decision( "navigation-copy" ) == OK );
  check_navigation_state( 1, 0, 0 );
  assert( fuse_host_select_tape_block( 1 ) == OK );
  check_navigation_state( 1, 1, 0 );
  assert( eject_tape_discarding_changes() == OK );
  check_navigation_state( 0, -1, 0 );
  assert( tape_filesystem.opens == tape_filesystem.closes );
}

static void
rom_tape_set_register( const char *name, unsigned int value )
{
  char command[64];
  snprintf( command, sizeof( command ), "set z80:%s 0x%x", name, value );
  assert( fuse_host_debugger_command( command, NULL ) == OK );
}

static void
rom_tape_set_byte( unsigned int address, unsigned int value )
{
  char command[64];
  snprintf( command, sizeof( command ), "set 0x%x 0x%x", address, value );
  assert( fuse_host_debugger_command( command, NULL ) == OK );
}

/* Enter and return stopped. The temporary breakpoint catches RET before any
   sentinel instruction executes; no HALT flag or test poll event is needed. */
static processor
run_rom_tape_routine( libspectrum_word pc )
{
  unsigned int frame;
  processor registers;
  fuse_run_result result;

  rom_tape_set_register( "sp", 0xaffc );
  rom_tape_set_byte( 0xaffc, 0x00 );
  rom_tape_set_byte( 0xaffd, 0x90 );
  rom_tape_set_register( "pc", pc );
  rom_tape_set_register( "iff1", 0 );
  rom_tape_set_register( "iff2", 0 );
  assert( fuse_host_debugger_command( "tbreak 0x9000", NULL ) == OK );
  assert( fuse_host_debugger_continue() == OK );
  for( frame = 0; frame < 1200; frame++ ) {
    result = fuse_run_frame();
    if( result == FUSE_RUN_DEBUGGER_STOPPED ) {
      assert( fuse_host_debugger_copy_registers( &registers ) == OK );
      assert( registers.pc.w == 0x9000 );
      return registers;
    }
    assert( result == FUSE_RUN_COMPLETED );
  }
  fprintf( stderr, "ROM tape routine %04x timed out after 1200 frames\n", pc );
  assert( 0 );
  return registers;
}

void
check_rom_tape_roundtrip( void )
{
  static const libspectrum_byte payload[] = {
    0x00, 0xff, 0x55, 0xaa, 0x01, 0x80, 0xfe, 0x7f,
    0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x10
  };
  settings_info *options;
  fuse_tape_info *info;
  libspectrum_tape *captured;
  libspectrum_tape_block *block;
  memory_file *exported;
  libspectrum_byte loaded[sizeof( payload )];
  processor registers;
  size_t i;
  int model;

  assert( fuse_host_copy_settings( &options ) == OK );
  options->tape_traps = options->accelerate_loader = 0;
  options->detect_loader = options->fastload = 0;
  assert( fuse_host_apply_settings( options, 0 ) == OK );
  fuse_host_free_settings( options );
  for( model = 0; model < 2; model++ ) {
    assert( fuse_host_select_machine( model ? "128" : "48" ) == OK );
    assert( fuse_host_debugger_break() == OK );
    assert( fuse_run_frame() == FUSE_RUN_DEBUGGER_STOPPED );
    if( model ) /* 128K's 48 BASIC ROM */
      assert( fuse_host_debugger_command( "out 0x7ffd 0x10", NULL ) == OK );
    for( i = 0; i < sizeof( payload ); i++ )
      rom_tape_set_byte( 0x8000 + i, payload[i] );
    rom_tape_set_register( "ix", 0x8000 );
    rom_tape_set_register( "de", sizeof( payload ) );
    rom_tape_set_register( "iy", 0x5c3a );
    rom_tape_set_register( "af", 0xff00 );
    assert( fuse_host_start_tape_recording() == OK );
    run_rom_tape_routine( 0x04c2 ); /* SA-BYTES: capture genuine MIC edges */
    assert( fuse_host_stop_tape_recording() == OK );
    assert( fuse_host_copy_tape_info( &info ) == OK );
    assert( info->present && info->modified && !info->recording );
    fuse_tape_info_free( info );
    assert( fuse_host_save_tape_file( "rom-save.pzx" ) == OK );
    exported = memory_find( "rom-save.pzx" ); assert( exported );
    assert( !exported->handles );
    captured = libspectrum_tape_alloc(); assert( captured );
    assert( !libspectrum_tape_read( captured, exported->data, exported->length,
                                    LIBSPECTRUM_ID_TAPE_PZX, NULL ) );
    /* No trap-generated ROM data block may substitute for captured pulses. */
    block = libspectrum_tape_current_block( captured );
    assert( block && libspectrum_tape_block_type( block ) ==
                    LIBSPECTRUM_TAPE_BLOCK_PULSE_SEQUENCE );
    libspectrum_tape_free( captured );
    assert( eject_tape_discarding_changes() == OK );
    assert( insert_tape_without_decision( "rom-save.pzx" ) == OK );
    for( i = 0; i < sizeof( payload ); i++ ) rom_tape_set_byte( 0x8000 + i, 0 );
    rom_tape_set_register( "ix", 0x8000 );
    rom_tape_set_register( "de", sizeof( payload ) );
    rom_tape_set_register( "af", 0xff01 ); /* FF flag, carry selects LOAD */
    assert( fuse_host_toggle_tape_play() == OK );
    registers = run_rom_tape_routine( 0x0556 ); /* LD-BYTES */
    assert( ( registers.af.b.l & 1 ) && !registers.de.w &&
            registers.ix.w == 0x8000 + sizeof( payload ) );
    assert( fuse_host_copy_memory( 0x8000, sizeof( loaded ), loaded ) == OK );
    assert( !memcmp( loaded, payload, sizeof( payload ) ) );
    assert( fuse_host_stop_tape() == OK );
    assert( eject_tape_discarding_changes() == OK );
    assert( fuse_host_debugger_continue() == OK );
  }
}

static void
check_deck_state( int block, int playing, int recording, int modified,
                  size_t blocks )
{
  fuse_tape_info *info;
  assert( fuse_host_copy_tape_info( &info ) == OK );
  if( info->block_count != blocks )
    fprintf( stderr, "Deck blocks: expected %lu, got %lu (block=%d playing=%d recording=%d modified=%d)\n",
             (unsigned long)blocks, (unsigned long)info->block_count,
             info->current_block, info->playing, info->recording, info->modified );
  assert( info->present == ( blocks != 0 ) && info->block_count == blocks );
  assert( info->current_block == block && info->playing == playing );
  assert( info->recording == recording && info->modified == modified );
  fuse_tape_info_free( info );
}

/* User-visible deck workflow: all engine access goes through host APIs.
   Backend fault injection replaces reliance on native directory failures. */
void
check_tape_deck( void )
{
  static const unsigned char bytes[] = { 2, 0, 0xff, 0xff, 2, 0, 0, 0 };
  void *handle;
  size_t written, recorded_blocks, reopened_blocks;
  fuse_tape_info *info;
  settings_info *options;

  handle = memory_open( "deck.tap", COMPAT_FILE_WRITE ); assert( handle );
  assert( !memory_write( handle, bytes, sizeof( bytes ), &written ) );
  assert( written == sizeof( bytes ) && !memory_close( handle ) );
  tape_filesystem.chunk = 3;
  assert( fuse_host_stop_tape() == OK );
  assert( fuse_host_rewind_tape() == OK );
  assert( eject_tape_discarding_changes() == OK );
  assert( fuse_host_toggle_tape_play() == FUSE_HOST_OPERATION_MEDIA_ERROR );
  assert( fuse_host_select_tape_block( -1 ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_tape_file( NULL ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  assert( fuse_host_save_tape_file( "" ) == FUSE_HOST_OPERATION_INVALID_ARGUMENT );
  check_deck_state( -1, 0, 0, 0, 0 );
  assert( insert_tape_without_decision( "deck.tap" ) == OK );
  assert( fuse_host_toggle_tape_play() == OK );
  check_deck_state( 0, 1, 0, 0, 2 );
  assert( fuse_host_select_tape_block( 1 ) == OK );
  check_deck_state( 1, 1, 0, 0, 2 );
  assert( fuse_host_select_tape_block( 99 ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  check_deck_state( 1, 1, 0, 0, 2 );
  assert( fuse_host_rewind_tape() == OK );
  check_deck_state( 0, 1, 0, 0, 2 );
  assert( fuse_host_toggle_tape_play() == OK );
  check_deck_state( 0, 0, 0, 0, 2 );
  assert( fuse_host_toggle_tape_play() == OK );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  tape_filesystem.fail_write = 1;
  assert( fuse_host_save_tape_file( "failed-copy.tzx" ) == FUSE_HOST_OPERATION_MEDIA_ERROR );
  tape_filesystem.fail_write = 0;
  check_deck_state( 0, 1, 0, 0, 2 );
  assert( fuse_host_save_tape_file( "deck-copy.tzx" ) == OK );
  check_deck_state( 0, 1, 0, 0, 2 );
  assert( fuse_host_stop_tape() == OK );
  assert( fuse_host_stop_tape() == OK );
  check_deck_state( 0, 0, 0, 0, 2 );

  /* Explicit pulse recording requires the traps-disabled host workflow. */
  assert( fuse_host_copy_settings( &options ) == OK );
  options->tape_traps = 0;
  assert( fuse_host_apply_settings( options, 0 ) == OK );
  fuse_host_free_settings( options );
  assert( fuse_host_start_tape_recording() == OK );
  assert( fuse_host_stop_tape() == OK ); /* Playback Stop is not Recording Stop. */
  check_deck_state( 0, 0, 1, 0, 2 );
  assert( fuse_run_tstates( 37 ) == FUSE_RUN_COMPLETED );
  tape_filesystem.fail_write = 1;
  assert( fuse_host_save_tape_file( "failed-recording.pzx" ) ==
          FUSE_HOST_OPERATION_MEDIA_ERROR );
  tape_filesystem.fail_write = 0;
  assert( fuse_host_copy_tape_info( &info ) == OK );
  assert( info->present && !info->playing && !info->recording && info->modified );
  recorded_blocks = info->block_count;
  assert( recorded_blocks > 2 );
  fuse_tape_info_free( info );
  assert( fuse_host_select_tape_block( recorded_blocks - 1 ) == OK );
  assert( fuse_host_save_tape_file( "deck-recorded.pzx" ) == OK );
  check_deck_state( recorded_blocks - 1, 0, 0, 0, recorded_blocks );
  assert( fuse_host_start_tape_recording() == OK );
  assert( fuse_run_tstates( 11 ) == FUSE_RUN_COMPLETED );
  assert( eject_tape_discarding_changes() == OK );
  check_deck_state( -1, 0, 0, 0, 0 );
  assert( insert_tape_without_decision( "deck-recorded.pzx" ) == OK );
  assert( fuse_host_select_tape_block( recorded_blocks - 1 ) == OK );
  /* PZX expands ROM blocks into pulse/data/pause rows on import. */
  assert( fuse_host_copy_tape_info( &info ) == OK );
  assert( info->block_count >= recorded_blocks );
  reopened_blocks = info->block_count;
  fuse_tape_info_free( info );
  check_deck_state( recorded_blocks - 1, 0, 0, 0, reopened_blocks );
  assert( eject_tape_discarding_changes() == OK );
  assert( insert_tape_without_decision( "deck-copy.tzx" ) == OK );
  check_deck_state( 0, 0, 0, 0, 2 );
  assert( eject_tape_discarding_changes() == OK );
  check_deck_state( -1, 0, 0, 0, 0 );
}

/* Deliberately internal: preserve exact edge scheduling, phantom-typist
   interaction, pulse durations and host-versus-legacy implementation parity. */
void
check_tape_deck_internal( int host )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff, 2, 0, 0, 0 };
  libspectrum_tape_block *block;
  struct deck_edges edges, changed;
  libspectrum_dword clock = tstates;
  assert( !tape_read_buffer( bytes, sizeof(bytes), LIBSPECTRUM_ID_TAPE_TAP, "deck.tap", 0 ) );
  phantom_typist_activate( machine_current->machine, 0 );
  assert( phantom_typist_is_active() );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) );
  /* Deactivation releases keys now, but the state settles at its frame tick. */
  phantom_typist_frame();
  assert( !phantom_typist_is_active() );
  edges = deck_edges(); assert( edges.count == 1 && tstates == clock );
  /* Navigation must not reschedule the pending edge, even on failure. */
  assert( !select_deck_block( host, 1 ) );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( select_deck_block( host, 99 ) == 1 );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( !deck_call( host, fuse_host_rewind_tape, tape_rewind ) );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) );
  assert( !deck_edges().count );
  assert( !deck_call( host, fuse_host_toggle_tape_play, manual_tape_toggle ) );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  block = libspectrum_tape_current_block( tape ); tape_modified = 1;
  assert( save_deck( host, "missing-directory/deck.tzx" ) == 1 );
  assert( tape_modified && tape_is_playing() && libspectrum_tape_current_block( tape ) == block );
  edges = deck_edges(); assert( edges.count == 1 );
  assert( !save_deck( host, "deck-copy.tzx" ) );
  changed = deck_edges(); assert( changed.count == 1 && changed.when == edges.when );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) );
  assert( !deck_edges().count );
  /* A failed export must still flush the exact outstanding recording hold. */
  assert( !tape_record_start() );
  tstates += 37;
  assert( save_deck( host, "missing-directory/recorded.pzx" ) == 1 );
  assert( !tape_recording && tape_modified && tape_present() );
  assert( !select_deck_block( host, 2 ) );
  block = libspectrum_tape_current_block( tape );
  assert( libspectrum_tape_block_type( block ) == LIBSPECTRUM_TAPE_BLOCK_PAUSE );
  assert( libspectrum_tape_block_pause_tstates( block ) == 37 );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
  assert( !remove( "deck-copy.tzx" ) );
}

static size_t
check_recording_state( int present, int playing, int recording, int modified )
{
  fuse_tape_info *info;
  size_t blocks;
  assert( fuse_host_copy_tape_info( &info ) == OK );
  assert( info->present == present && info->playing == playing );
  assert( info->recording == recording && info->modified == modified );
  blocks = info->block_count;
  assert( present ? blocks > 0 : blocks == 0 );
  fuse_tape_info_free( info );
  return blocks;
}

/* Host-visible recording lifecycle, with real bounded execution rather than
   injected timestamps or microphone edges. Exact waveforms stay below. */
void
check_tape_recording( void )
{
  static const unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  void *handle;
  size_t written, blocks;

  assert( fuse_host_stop_tape_recording() == OK );
  assert( eject_tape_discarding_changes() == OK );
  check_recording_state( 0, 0, 0, 0 );
  /* An initialized empty deck is a valid destination. Start is idempotent. */
  assert( fuse_host_start_tape_recording() == OK );
  assert( fuse_host_start_tape_recording() == OK );
  check_recording_state( 0, 0, 1, 0 );
  assert( fuse_run_tstates( 31 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_tape_recording() == OK );
  check_recording_state( 1, 0, 0, 1 );
  assert( fuse_host_stop_tape_recording() == OK );
  assert( fuse_host_save_tape_file( "empty-recording.pzx" ) == OK );
  check_recording_state( 1, 0, 0, 0 );
  assert( eject_tape_discarding_changes() == OK );
  assert( insert_tape_without_decision( "empty-recording.pzx" ) == OK );
  check_recording_state( 1, 0, 0, 0 );
  assert( eject_tape_discarding_changes() == OK );

  handle = memory_open( "recording.tap", COMPAT_FILE_WRITE ); assert( handle );
  assert( !memory_write( handle, bytes, sizeof( bytes ), &written ) );
  assert( written == sizeof( bytes ) && !memory_close( handle ) );
  tape_filesystem.chunk = 3;
  assert( insert_tape_without_decision( "recording.tap" ) == OK );
  assert( fuse_host_toggle_tape_play() == OK );
  assert( fuse_run_tstates( 4000 ) == FUSE_RUN_COMPLETED );
  blocks = check_recording_state( 1, 1, 0, 0 );
  assert( fuse_host_start_tape_recording() == OK );
  check_recording_state( 1, 1, 1, 0 );
  assert( fuse_run_tstates( 40 ) == FUSE_RUN_COMPLETED );
  assert( fuse_host_stop_tape_recording() == OK );
  assert( check_recording_state( 1, 1, 0, 1 ) > blocks );
  assert( fuse_host_save_tape_file( "recording-copy.pzx" ) == OK );
  blocks = check_recording_state( 1, 1, 0, 0 );
  assert( fuse_host_stop_tape_recording() == OK );
  /* No execution between Start and Stop: no new hold or dirty state. */
  assert( fuse_host_start_tape_recording() == OK );
  assert( fuse_host_stop_tape_recording() == OK );
  assert( check_recording_state( 1, 1, 0, 0 ) == blocks );
  assert( fuse_host_stop_tape() == OK );
  assert( eject_tape_discarding_changes() == OK );
  assert( insert_tape_without_decision( "recording-copy.pzx" ) == OK );
  check_recording_state( 1, 0, 0, 0 );
  assert( eject_tape_discarding_changes() == OK );
  check_recording_state( 0, 0, 0, 0 );
}

/* Internal parity: missing recorder, RZX exclusion, exact pulse intervals,
   nested pauses and preservation of the pending playback edge. */
void
check_tape_recording_internal( int host )
{
  unsigned char bytes[] = { 2, 0, 0xff, 0xff };
  libspectrum_tape_block *block;
  struct deck_edges edges, after;
  tape_record_set_tape( NULL );
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); tape_record_set_tape( tape );
  rzx_playback = 1;
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); rzx_playback = 0; rzx_recording = 1;
  assert( deck_call( host, fuse_host_start_tape_recording, tape_record_start ) == 1 );
  assert( !tape_recording ); rzx_recording = 0;
  /* RZX exclusion must not disrupt an already active recorder; the injected
     unchanged level must be retained as an exact 31-T-state hold. */
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  assert( tape_recording ); rzx_playback = 1;
  assert( !deck_call( host, fuse_host_start_tape_recording, tape_record_start ) );
  rzx_playback = 0; tstates += 31;
  assert( !deck_call( host, fuse_host_stop_tape_recording, tape_record_stop ) );
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
  assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( !deck_call( host, fuse_host_stop_tape, tape_stop ) );
  assert( !deck_call( host, eject_tape_discarding_changes, ui_tape_close ) );
}

void
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

void
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

void
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

int
fail_autoload_reset( int hard_reset, libspectrum_snap *snap )
{
  assert( !hard_reset && !snap );
  return 37;
}

void
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
