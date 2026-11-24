/* floppysaveuitest.c: shared floppy target-save and eject regression tests
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fuse.h"
#include "fuse-internal.h"
#include "menu.h"
#include "peripherals/disk/fdd.h"
#include "ui/ui.h"
#include "peripherals/disk/drive.h"
#include "utils.h"
/* menu.h re-includes generated config.h; enable assertions after all headers. */
#undef NDEBUG
#include <assert.h>

/* A unit-only extra slot exercises the shared workflow through the host API,
   not real controller timing/availability (covered separately). Existing drive
   descriptors remain untouched. */
#define TEST_CONTROLLER FUSE_DISK_CONTROLLER_PLUS3
#define TEST_SLOT 15
static fdd_t fdd;
static const fdd_params_t params = { 1, 2, 40 };
static unsigned char rom_bytes[0x4000];
static char drive_name[] = "Test floppy";
static int drive_available = 1;
static const char *selected_filename;
static int caller_pauses, writes, filenames, overwrites, eject_questions, fail_write;
static int fail_read, file_frees;
static ui_confirm_save_t overwrite_choice = UI_CONFIRM_SAVE_SAVE;
static ui_confirm_save_t eject_choice = UI_CONFIRM_SAVE_SAVE;

static const fdd_params_t *get_params( void ) { return &params; }
static int is_available( void ) { return drive_available; }
static disk_drive_info_t drive = {
  drive_name, TEST_CONTROLLER, TEST_SLOT,
  UI_MENU_ITEM_INVALID, UI_MENU_ITEM_INVALID, UI_MENU_ITEM_INVALID,
  UI_MENU_ITEM_INVALID, UI_MENU_ITEM_INVALID,
  is_available, get_params, NULL, NULL, &fdd
};

int test_floppy_file_read( utils_file *file );
void test_floppy_file_free( utils_file *file );
int test_floppy_disk_write( disk_t *disk, const char *filename );
char *test_floppy_save_filename( const char *title );
ui_confirm_save_t test_floppy_save_confirm( const char *format, ... );

int
test_floppy_file_read( utils_file *file )
{
  assert( file->filename );
  return fail_read ? fail_read : utils_file_read( file );
}

void
test_floppy_file_free( utils_file *file )
{
  file_frees++;
  utils_file_free( file );
}

int
test_floppy_disk_write( disk_t *disk, const char *filename )
{
  assert( fuse_emulation_paused == caller_pauses + 1 );
  writes++;
  if( fail_write ) return disk->status = DISK_WRFILE;
  return disk_write( disk, filename );
}

char *
test_floppy_save_filename( const char *title )
{
  (void)title;
  assert( fuse_emulation_paused == caller_pauses + 1 );
  filenames++;
  return selected_filename ? utils_safe_strdup( selected_filename ) : NULL;
}

ui_confirm_save_t
test_floppy_save_confirm( const char *format, ... )
{
  if( strstr( format, "already exists" ) ) {
    assert( fuse_emulation_paused == caller_pauses + 1 );
    overwrites++; return overwrite_choice;
  }
  assert( strstr( format, "has been modified" ) );
  assert( fuse_emulation_paused == caller_pauses );
  eject_questions++; return eject_choice;
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
  writes = filenames = overwrites = eject_questions = file_frees = 0;
  selected_filename = NULL;
}

#ifndef FUSE_MEDIA_SAVE_BASELINE
static int
legacy_result( fuse_host_operation_result result )
{
  assert( result == FUSE_HOST_OPERATION_OK || result == FUSE_HOST_OPERATION_MEDIA_ERROR );
  return result != FUSE_HOST_OPERATION_OK;
}
#endif

static int
new_disk( void )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  return disk_drive_insert( &drive, NULL, 0 );
#else
  fuse_media_decision_id request;
  fuse_host_operation_result result = fuse_host_new_disk( TEST_CONTROLLER, TEST_SLOT, &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
    fuse_media_decision_choice reply = eject_choice == UI_CONFIRM_SAVE_SAVE ? FUSE_MEDIA_DECISION_SAVE :
      eject_choice == UI_CONFIRM_SAVE_CANCEL ? FUSE_MEDIA_DECISION_CANCEL : FUSE_MEDIA_DECISION_DISCARD;
    assert( request && !eject_questions );
    result = fuse_host_answer_disk_eject( request, reply, NULL, &(fuse_open_request){ 0 } );
    assert( result == ( reply == FUSE_MEDIA_DECISION_CANCEL ? FUSE_HOST_OPERATION_CANCELLED :
              reply == FUSE_MEDIA_DECISION_SAVE && fail_write ? FUSE_HOST_OPERATION_MEDIA_ERROR :
                                                                FUSE_HOST_OPERATION_OK ) );
    return 0; /* Match the legacy lifecycle report, not the host result. */
  }
  assert( !request );
  return legacy_result( result );
#endif
}

static int
eject_disk( void )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  return disk_drive_eject( TEST_CONTROLLER, TEST_SLOT );
#else
  fuse_media_decision_id request;
  fuse_host_operation_result result = fuse_host_eject_disk( TEST_CONTROLLER, TEST_SLOT, &request );
  if( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
    fuse_media_decision_choice reply = eject_choice == UI_CONFIRM_SAVE_SAVE ? FUSE_MEDIA_DECISION_SAVE :
      eject_choice == UI_CONFIRM_SAVE_CANCEL ? FUSE_MEDIA_DECISION_CANCEL : FUSE_MEDIA_DECISION_DISCARD;
    assert( request && !eject_questions );
    result = fuse_host_answer_disk_eject( request, reply, NULL,
                                          &(fuse_open_request){ 0 } );
    if( result == FUSE_HOST_OPERATION_CANCELLED ) return 1;
  } else assert( !request );
  return legacy_result( result );
#endif
}

static int
set_flipped( int flipped )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  return disk_drive_flip( TEST_CONTROLLER, TEST_SLOT, flipped );
#else
  return legacy_result( fuse_host_set_disk_flipped( TEST_CONTROLLER, TEST_SLOT, flipped ) );
#endif
}

static int
set_write_protected( int write_protected )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  return disk_drive_writeprotect( TEST_CONTROLLER, TEST_SLOT, write_protected );
#else
  return legacy_result( fuse_host_set_disk_write_protected( TEST_CONTROLLER, TEST_SLOT, write_protected ) );
#endif
}

static int
save_to( const char *filename )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  /* Exercise the chooser path; also used against the committed old workflow. */
  selected_filename = filename;
  return disk_drive_save( TEST_CONTROLLER, TEST_SLOT, filename != NULL );
#else
  return legacy_result( fuse_host_save_disk_file( TEST_CONTROLLER, TEST_SLOT, filename ) );
#endif
}

static int
eject_prompt_count( void )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  return 1;
#else
  return 0;
#endif
}

static void
check_target_chooser( int expected )
{
#ifdef FUSE_MEDIA_SAVE_BASELINE
  assert( filenames == expected );
#else
  (void)expected;
  assert( filenames == 0 ); /* A chosen-target command never selects a target. */
#endif
}

static void
modify( unsigned char value )
{
  assert( fdd.loaded && fdd.disk.data );
  DISK_SET_TRACK( &fdd.disk, 0, 0 );
  fdd.disk.i = 0;
  /* Supply deterministic FDC preconditions, then use the actual write path. */
  fdd.selected = fdd.ready = fdd.loadhead = 1;
  fdd.data = value; fdd.marks = 0;
  assert( fdd_write_data( &fdd ) == FDD_OK );
  fdd.selected = fdd.ready = fdd.loadhead = 0;
  assert( fdd.disk.dirty && fdd.disk.track[0] == value );
}

static void
check_retained( libspectrum_byte *data, const char *name, unsigned char value )
{
  assert( fdd.loaded && fdd.disk.dirty && fdd.disk.data == data );
  assert( fdd.disk.filename && !strcmp( fdd.disk.filename, name ) );
  DISK_SET_TRACK( &fdd.disk, 0, 0 );
  assert( fdd.disk.track[0] == value );
  assert( fuse_emulation_paused == caller_pauses );
}

static void
check_written( const char *name, unsigned char value )
{
  disk_t disk = { 0 };
  assert( disk_open( &disk, name, 0, 0 ) == DISK_OK );
  DISK_SET_TRACK( &disk, 0, 0 );
  assert( disk.track[0] == value );
  disk_close( &disk );
}

static void
report( const char *label, int result )
{
  printf( "%s result=%d loaded=%d dirty=%d type=%d name=%s pause=%d\n",
          label, result, fdd.loaded, fdd.disk.dirty, fdd.disk.type,
          fdd.disk.filename ? fdd.disk.filename : "-", fuse_emulation_paused );
  assert( fuse_emulation_paused == caller_pauses );
}

static fuse_disk_drive_info *
copy_info( void )
{
  fuse_disk_drive_info *info = NULL;
  assert( fuse_host_copy_disk_drive_info( TEST_CONTROLLER, TEST_SLOT, &info ) == FUSE_HOST_OPERATION_OK );
  assert( info && info->controller == TEST_CONTROLLER && info->slot == TEST_SLOT );
  return info;
}

static fuse_disk_drive_info *
check_observations( void )
{
  fuse_disk_drive_info *cold = copy_info(), *info, *saved;
  fdd_type_t type = fdd.type;
  assert( cold->available && cold->enabled && !cold->loaded && !cold->filename );
  assert( cold->name != drive.name && !strcmp( cold->name, "Test floppy" ) );
  drive_name[0] = 'X';
  assert( !strcmp( cold->name, "Test floppy" ) ); drive_name[0] = 'T';
  drive_available = 0; info = copy_info();
  assert( !info->available && info->enabled && !info->loaded && cold->available );
  fuse_disk_drive_info_free( info ); drive_available = 1;
  fdd.type = FDD_TYPE_NONE; info = copy_info();
  assert( info->available && !info->enabled && !info->loaded );
  fuse_disk_drive_info_free( info ); fdd.type = type;
  assert( !new_disk() );
  info = copy_info();
  assert( info->loaded && !info->filename && !info->flipped && !info->write_protected );
  fuse_disk_drive_info_free( info );
  assert( !set_flipped( 1 ) && !set_write_protected( 1 ) );
  assert( !save_to( "observed.udi" ) ); saved = copy_info();
  assert( saved->loaded && saved->flipped && saved->write_protected );
  assert( !strcmp( saved->filename, "observed.udi" ) && saved->filename != fdd.disk.filename );
  assert( !save_to( "renamed.udi" ) ); info = copy_info();
  assert( !strcmp( info->filename, "renamed.udi" ) && !strcmp( saved->filename, "observed.udi" ) );
  fuse_disk_drive_info_free( info );
  assert( !new_disk() ); info = copy_info();
  assert( info->loaded && !info->filename && !info->flipped && !info->write_protected );
  assert( saved->loaded && saved->flipped && saved->write_protected );
  fuse_disk_drive_info_free( info ); eject_choice = UI_CONFIRM_SAVE_DONTSAVE;
  assert( !eject_disk() ); info = copy_info();
  assert( !info->loaded && !info->filename );
  /* Report actual drive latches, not fabricated media-presence inference. */
  assert( info->flipped == !!fdd.upsidedown && info->write_protected == !!fdd.wrprot );
  assert( !cold->loaded && !cold->filename && saved->loaded );
  assert( !strcmp( saved->name, "Test floppy" ) && !strcmp( saved->filename, "observed.udi" ) );
  fuse_disk_drive_info_free( cold ); fuse_disk_drive_info_free( info );
  fuse_disk_drive_info_free( NULL );
  assert( !remove( "observed.udi" ) && !remove( "renamed.udi" ) );
  return saved;
}

static void
check_lifecycle( void )
{
  libspectrum_byte *data;
  assert( !new_disk() && fdd.loaded && fdd.disk.dirty && !fdd.disk.filename );
  assert( fdd.disk.sides == params.heads && fdd.disk.cylinders == params.cylinders );
  modify( 0x31 ); reset_counts();
  assert( !save_to( "life.udi" ) );
  assert( !fdd.disk.dirty );
  assert( !set_write_protected( 1 ) && fdd.wrprot && !fdd.disk.dirty );
  assert( !set_flipped( 1 ) && fdd.upsidedown && !fdd.disk.dirty );
  assert( !set_flipped( 0 ) && !fdd.upsidedown );
  assert( !set_write_protected( 0 ) && !fdd.wrprot && !fdd.disk.dirty );
  modify( 0x42 ); data = fdd.disk.data;
  reset_counts(); eject_choice = UI_CONFIRM_SAVE_CANCEL;
  assert( !new_disk() && !writes && eject_questions == eject_prompt_count() );
  check_retained( data, "life.udi", 0x42 ); report( "new-cancel-retained", 0 );
  reset_counts(); eject_choice = UI_CONFIRM_SAVE_SAVE; fail_write = 1;
  assert( !new_disk() && writes == 1 && eject_questions == eject_prompt_count() );
  check_retained( data, "life.udi", 0x42 ); report( "new-save-failed-retained", 0 );
  reset_counts(); eject_choice = UI_CONFIRM_SAVE_DONTSAVE; fail_write = 0;
  assert( !new_disk() && !writes && eject_questions == eject_prompt_count() );
  assert( fdd.loaded && fdd.disk.dirty && !fdd.disk.filename );
  check_written( "life.udi", 0x31 );
  assert( !save_to( "new.udi" ) && !fdd.disk.dirty );
  assert( !eject_disk() && !fdd.loaded );
  assert( !eject_disk() );
  assert( set_flipped( 1 ) == 1 && set_write_protected( 1 ) == 1 );
  assert( fuse_emulation_paused == caller_pauses );
  assert( !remove( "life.udi" ) && !remove( "new.udi" ) );
}

static void
check_menu_controllers( void )
{
  static const fuse_disk_controller controllers[] = {
    FUSE_DISK_CONTROLLER_PLUS3, FUSE_DISK_CONTROLLER_BETA, FUSE_DISK_CONTROLLER_PLUSD,
    FUSE_DISK_CONTROLLER_OPUS, FUSE_DISK_CONTROLLER_DISCIPLE, FUSE_DISK_CONTROLLER_DIDAKTIK
  };
  static const int groups[] = { 0, 1, 2, 4, 5, 6 };
  unsigned int i;

  for( i = 0; i < sizeof(controllers) / sizeof(controllers[0]); i++ ) {
    int action = ( groups[i] << 4 ) | TEST_SLOT;
    drive.controller_index = controllers[i];
    menu_media_insert_new( action + 1 );
    assert( fdd.loaded ); modify( 0xa0 + i );
    reset_counts(); selected_filename = "menu.udi";
    menu_media_save( ( action | 0x100 ) + 1 );
    assert( writes == 1 && filenames == 1 && !fdd.disk.dirty );
    check_written( "menu.udi", 0xa0 + i );
    menu_media_writeprotect( ( action | 0x100 ) + 1 );
    assert( fdd.wrprot );
    menu_media_writeprotect( action + 1 ); assert( !fdd.wrprot );
    if( controllers[i] == FUSE_DISK_CONTROLLER_OPUS ) {
      /* Action group3 remains Microdrive, not the engine enum's Opus value. */
      menu_media_flip( ( 0x30 | TEST_SLOT | 0x100 ) + 1 );
      assert( !fdd.upsidedown );
    }
    menu_media_flip( ( action | 0x100 ) + 1 ); assert( fdd.upsidedown );
    menu_media_flip( action + 1 ); assert( !fdd.upsidedown );
    menu_media_eject( action + 1 ); assert( !fdd.loaded );
    assert( !remove( "menu.udi" ) );
    assert( fuse_emulation_paused == caller_pauses );
  }
  menu_media_insert( ( 0xf0 | TEST_SLOT ) + 1 );
  assert( fuse_emulation_paused == caller_pauses );
}

int
main( int argc, char **argv )
{
  fuse_host_resources resources = { NULL, acquire_rom };
  fuse_host_config *config;
  fuse_disk_drive_info *retained = NULL;
  libspectrum_byte *data;
  char target[] = "first.udi";
  char sentinel[8];
  FILE *file;
  int result, nested = 1, lifecycle = 0, observations = 0, i;
  assert( argc >= 2 && argc <= 4 );
  for( i = 2; i < argc; i++ ) {
    if( !strcmp( argv[i], "--unpaused" ) ) nested = 0;
    else if( !strcmp( argv[i], "--observations" ) ) observations = 1;
    else { assert( !strcmp( argv[i], "--lifecycle" ) ); lifecycle = 1; }
  }
  file = fopen( argv[1], "rb" ); assert( file );
  assert( fread( rom_bytes, 1, sizeof(rom_bytes), file ) == sizeof(rom_bytes) );
  assert( !fclose( file ) );
  config = fuse_host_config_alloc();
  assert( !fuse_host_config_set_machine( config, "48" ) );
  assert( !fuse_host_config_set_sound( config, 0 ) );
  assert( fuse_host_prepare( config, &resources ) == FUSE_INIT_READY );
  fuse_host_config_free( config );
  assert( fdd_init( &fdd, FDD_SHUGART, &params, 0 ) == FDD_OK );
  assert( !disk_drive_register( &drive ) );
  if( nested ) assert( !fuse_emulation_pause() && !fuse_emulation_pause() );
  caller_pauses = fuse_emulation_paused;
  if( lifecycle ) { check_lifecycle(); goto done; }
  if( observations ) { retained = check_observations(); goto done; }
  assert( !new_disk() );
  modify( 0x5a );

  reset_counts(); fail_read = 37;
  data = fdd.disk.data;
  assert( disk_drive_insert( &drive, "failed-input.udi", 0 ) == 1 );
  assert( file_frees == 1 && !eject_questions && !writes );
  assert( fdd.loaded && fdd.disk.dirty && fdd.disk.data == data && !fdd.disk.filename );
  fail_read = 0;

  reset_counts();
  result = save_to( NULL ); assert( result == 1 && writes == 0 );
  check_target_chooser( 1 ); report( "unnamed-no-target", result );
  reset_counts();
  result = disk_drive_save( TEST_CONTROLLER, TEST_SLOT, 1 );
  assert( result == 1 && filenames == 1 && writes == 0 );
  report( "chooser-cancel", result );

  reset_counts();
  result = save_to( target ); assert( !result && writes == 1 && overwrites == 0 );
  check_target_chooser( 1 );
  target[0] = 'X';
  assert( !strcmp( fdd.disk.filename, "first.udi" ) && !fdd.disk.dirty );
  check_written( "first.udi", 0x5a ); report( "new-target", result );

  modify( 0x6b ); reset_counts();
  result = save_to( NULL ); assert( !result && writes == 1 && overwrites == 0 );
  check_target_chooser( 0 );
  assert( !fdd.disk.dirty ); check_written( "first.udi", 0x6b );
  report( "original-target", result );
  data = fdd.disk.data;
  file = fopen( "other.udi", "wb" ); assert( file );
  assert( fwrite( "KEEP", 1, 4, file ) == 4 && !fclose(file) );
  modify( 0x7c );
  reset_counts(); overwrite_choice = UI_CONFIRM_SAVE_DONTSAVE;
  selected_filename = "other.udi";
  result = disk_drive_save( TEST_CONTROLLER, TEST_SLOT, 1 );
  assert( result == 1 && writes == 0 && overwrites == 1 && filenames == 1 );
  check_retained( data, "first.udi", 0x7c );
  assert( fdd.disk.type == DISK_TYPE_NONE ); /* Existing pre-question mutation. */
  report( "overwrite-decline", result );
  reset_counts(); overwrite_choice = UI_CONFIRM_SAVE_CANCEL;
  selected_filename = "other.udi";
  result = disk_drive_save( TEST_CONTROLLER, TEST_SLOT, 1 );
  assert( result == 1 && writes == 0 && overwrites == 1 && filenames == 1 );
  check_retained( data, "first.udi", 0x7c );
  file = fopen( "other.udi", "rb" ); assert( file );
  assert( fread( sentinel, 1, 4, file ) == 4 && !memcmp(sentinel,"KEEP",4) && !fclose(file) );
  report( "overwrite-cancel", result );
  reset_counts();
#ifdef FUSE_MEDIA_SAVE_BASELINE
  overwrite_choice = UI_CONFIRM_SAVE_SAVE;
#else
  overwrite_choice = UI_CONFIRM_SAVE_CANCEL; /* Host already approved the path. */
#endif
  result = save_to( "other.udi" );
  assert( !result && writes == 1 );
#ifdef FUSE_MEDIA_SAVE_BASELINE
  assert( overwrites == 1 );
#else
  assert( !overwrites );
#endif
  check_target_chooser( 1 ); assert( !fdd.disk.dirty && !strcmp(fdd.disk.filename,"other.udi") );
  check_written( "other.udi", 0x7c ); report( "overwrite-save", result );

  modify( 0x8d ); reset_counts(); fail_write = 1;
  result = save_to( "failed.udi" ); assert( result == 1 && writes == 1 );
  check_target_chooser( 1 ); check_retained( data, "other.udi", 0x8d );
  assert( !compat_file_exists( "failed.udi" ) ); report( "write-failure", result );
  reset_counts(); fail_write = 0;
  result = save_to( "missing-directory/out.udi" ); assert( result == 1 && writes == 1 );
  check_target_chooser( 1 ); check_retained( data, "other.udi", 0x8d );
  report( "real-open-failure", result );

  reset_counts(); eject_choice = UI_CONFIRM_SAVE_SAVE; fail_write = 1;
  result = eject_disk();
  assert( result == 1 && writes == 1 && filenames == 0 && eject_questions == eject_prompt_count() );
  check_retained( data, "other.udi", 0x8d ); report( "eject-save-failure", result );
  reset_counts(); eject_choice = UI_CONFIRM_SAVE_CANCEL; fail_write = 0;
  result = eject_disk();
  assert( result == 1 && writes == 0 && filenames == 0 && eject_questions == eject_prompt_count() );
  check_retained( data, "other.udi", 0x8d ); report( "eject-cancel", result );
  reset_counts(); eject_choice = UI_CONFIRM_SAVE_SAVE;
  result = eject_disk();
  assert( !result && writes == 1 && filenames == 0 && eject_questions == eject_prompt_count() );
  assert( !fdd.loaded && !fdd.disk.data && !fdd.disk.filename );
  check_written( "other.udi", 0x8d ); report( "eject-save", result );

  assert( !disk_drive_insert( &drive, "other.udi", 0 ) );
  modify( 0x9e ); reset_counts(); eject_choice = UI_CONFIRM_SAVE_DONTSAVE;
  result = eject_disk();
  assert( !result && writes == 0 && filenames == 0 && eject_questions == eject_prompt_count() );
  assert( !fdd.loaded && !fdd.disk.data && !fdd.disk.filename );
  check_written( "other.udi", 0x8d ); report( "eject-discard", result );
  assert( !remove( "first.udi" ) && !remove( "other.udi" ) );
  check_menu_controllers();
done:
  if( nested ) assert( !fuse_emulation_unpause() && !fuse_emulation_unpause() );
  assert( !fuse_emulation_paused );
  fuse_shutdown(); fuse_shutdown();
  if( retained ) {
    assert( retained->loaded && retained->flipped && retained->write_protected );
    assert( !strcmp( retained->name, "Test floppy" ) && !strcmp( retained->filename, "observed.udi" ) );
    fuse_disk_drive_info_free( retained );
  }
  return 0;
}
