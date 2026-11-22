/* snapshot.c: snapshot handling routines
   Copyright (c) 1999-2012 Philip Kendall
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

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#include "config.h"

#include "libspectrum.h"

#include "display.h"
#include "fuse.h"
#include "fuse-engine.h"
#include "keyboard.h"
#include "machine.h"
#include "memory_pages.h"
#include "machines/specplus3.h"
#include "module.h"
#include "peripherals/joystick.h"
#include "pokefinder/pokemem.h"
#include "rzx.h"
#include "settings.h"
#include "snapshot.h"
#include "sound.h"
#include "ui/ui.h"
#include "utils.h"

int snapshot_read( const char *filename )
{
  utils_file file;
  libspectrum_snap *snap = libspectrum_snap_alloc();
  int error;

  error = utils_read_file( filename, &file );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  error = libspectrum_snap_read( snap, file.buffer, file.length,
				 LIBSPECTRUM_ID_UNKNOWN, filename );
  if( error ) {
    utils_close_file( &file ); libspectrum_snap_free( snap );
    return error;
  }

  utils_close_file( &file );

  error = snapshot_copy_from( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  error = libspectrum_snap_free( snap ); if( error ) return error;

  return 0;
}

int
snapshot_read_buffer( const unsigned char *buffer, size_t length,
		      libspectrum_id_t type )
{
  libspectrum_snap *snap = libspectrum_snap_alloc();
  int error;

  error = libspectrum_snap_read( snap, buffer, length, type, NULL );
  if( error ) { libspectrum_snap_free( snap ); return error; }
    
  error = snapshot_copy_from( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  error = libspectrum_snap_free( snap ); if( error ) return error;

  return 0;
}

static int
snapshot_restore( libspectrum_snap *snap, const joystick_type_t *outputs )
{
  int error;
  libspectrum_machine machine;

  /* Restored source state/time is not a continuation of consumed audio. */
  sound_suspend_subframe();
  specplus3_bus_invalidate();
  periph_disable_optional();
  joystick_snapshot_set_outputs( outputs );
  module_snapshot_enabled( snap );
  joystick_snapshot_set_outputs( NULL );

  machine = libspectrum_snap_machine( snap );

  settings_current.late_timings = libspectrum_snap_late_timings( snap );

  /* Drop stale banks; register this snapshot's ROMs between machine setup
     and peripheral reset, before ROM files are consulted. */
  machine_clear_snapshot_rom_banks();

  if( machine != machine_current->machine ) {
    error = machine_select_from_snapshot( machine, snap );
    if( error ) {
      ui_error( UI_ERROR_ERROR,
		"Loading a %s snapshot, but that's not available",
		libspectrum_machine_name( machine ) );
      return error;
    }
  } else {
    error = machine_reset_from_snapshot( 0, snap );
    if( error ) return error;
  }


  keyboard_release_all();

  specplus3_bus_invalidate();
  module_snapshot_from( snap );

  /* Need to reset memory_map_[read|write] after all modules have had a turn
     initialising from the snapshot */
  machine_current->memory_map();
  specplus3_bus_rebase();

  return 0;
}

int
snapshot_copy_from( libspectrum_snap *snap )
{
  return snapshot_restore( snap, NULL );
}

/* Keep decisions outside the restore pass: no machine or input state is
   changed while the host is answering. Walk in snapshot order, using the
   proposed outputs to decide which subsequent questions are still needed. */
static struct {
  libspectrum_snap *snap;
  char *filename;
  size_t index;
  enum { SNAPSHOT_FILE, SNAPSHOT_RZX_EMBEDDED, SNAPSHOT_RZX_EXTERNAL,
         SNAPSHOT_RZX_RECORDING, SNAPSHOT_RZX_ROLLBACK } source;
  int borrowed;
  fuse_media_decision_id id;
  joystick_type_t outputs[3];
} pending_snapshot;

void
snapshot_cancel_pending( void )
{
  if( pending_snapshot.snap && !pending_snapshot.borrowed )
    libspectrum_snap_free( pending_snapshot.snap );
  libspectrum_free( pending_snapshot.filename );
  pending_snapshot.snap = NULL;
  pending_snapshot.filename = NULL;
  pending_snapshot.id = 0;
  pending_snapshot.source = SNAPSHOT_FILE;
  pending_snapshot.borrowed = 0;
  fuse_engine_retire_decision( FUSE_DECISION_SNAPSHOT );
}

static void
snapshot_next_joystick( fuse_open_request *request )
{
  size_t count = libspectrum_snap_joystick_active_count( pending_snapshot.snap );
  while( !rzx_playback && pending_snapshot.index < count ) {
    joystick_type_t type = joystick_snapshot_type( libspectrum_snap_joystick_list(
      pending_snapshot.snap, pending_snapshot.index ) );
    if( type != JOYSTICK_TYPE_NONE && type != pending_snapshot.outputs[0] &&
        type != pending_snapshot.outputs[1] &&
        type != pending_snapshot.outputs[2] ) {
      request->id = pending_snapshot.id =
        fuse_engine_request_decision( FUSE_DECISION_SNAPSHOT );
      request->question = FUSE_OPEN_MAP_JOYSTICK;
      request->detail.joystick.type = libspectrum_snap_joystick_list(
        pending_snapshot.snap, pending_snapshot.index );
      request->detail.joystick.inputs = libspectrum_snap_joystick_inputs(
        pending_snapshot.snap, pending_snapshot.index );
      return;
    }
    pending_snapshot.index++;
  }
}

static int
snapshot_stage_restore( fuse_open_request *request )
{
  pending_snapshot.index = 0;
  pending_snapshot.outputs[0] = settings_current.joystick_1_output;
  pending_snapshot.outputs[1] = settings_current.joystick_2_output;
  pending_snapshot.outputs[2] = settings_current.joystick_keyboard_output;
  snapshot_next_joystick( request );
  return request->id ? 0 :
         snapshot_restore( pending_snapshot.snap, pending_snapshot.outputs );
}

int
snapshot_request_rzx_restore( libspectrum_snap *snap,
                              fuse_open_request *request )
{
  int error;
  pending_snapshot.snap = snap;
  pending_snapshot.source = SNAPSHOT_RZX_EMBEDDED;
  pending_snapshot.borrowed = 1;
  error = snapshot_stage_restore( request );
  if( !request->id ) snapshot_cancel_pending();
  return error;
}

static int
snapshot_read_pending_path( const char *filename )
{
  utils_file file;
  int error = utils_read_file( filename, &file );
  if( !error ) {
    pending_snapshot.snap = libspectrum_snap_alloc();
    error = libspectrum_snap_read( pending_snapshot.snap, file.buffer,
                                   file.length, LIBSPECTRUM_ID_UNKNOWN,
                                   filename );
  }
  utils_close_file( &file );
  return error;
}

int
snapshot_request_rzx_external( const char *filename,
                               fuse_open_request *request )
{
  int error = snapshot_read_pending_path( filename );
  pending_snapshot.source = SNAPSHOT_RZX_EXTERNAL;
  if( !error ) error = snapshot_stage_restore( request );
  if( !request->id ) snapshot_cancel_pending();
  return error;
}

int
snapshot_request_rzx_recording( libspectrum_snap *snap, const char *filename,
                                fuse_open_request *request )
{
  int error = 0;
  pending_snapshot.source = SNAPSHOT_RZX_RECORDING;
  if( snap ) {
    pending_snapshot.snap = snap;
    pending_snapshot.borrowed = 1;
  } else {
    error = snapshot_read_pending_path( filename );
  }
  if( !error ) error = snapshot_stage_restore( request );
  if( !request->id ) snapshot_cancel_pending();
  return error;
}

int
snapshot_rzx_rollback_pending( void )
{
  return pending_snapshot.source == SNAPSHOT_RZX_ROLLBACK;
}

void
snapshot_cancel_rzx_rollback( void )
{
  if( snapshot_rzx_rollback_pending() )
    snapshot_cancel_pending();
}

int
snapshot_request_rzx_rollback( libspectrum_snap *snap,
                               fuse_open_request *request )
{
  int error;
  pending_snapshot.snap = snap;
  pending_snapshot.borrowed = 1;
  pending_snapshot.source = SNAPSHOT_RZX_ROLLBACK;
  error = snapshot_stage_restore( request );
  if( !request->id ) snapshot_cancel_pending();
  return error;
}

int
snapshot_request_open( const libspectrum_file *file,
                       fuse_open_request *request )
{
  int error;
  pending_snapshot.snap = libspectrum_snap_alloc();
  error = libspectrum_snap_read( pending_snapshot.snap, file->buffer, file->length,
                                file->type, NULL );
  if( !error ) {
    error = snapshot_stage_restore( request );
    if( request->id ) {
      pending_snapshot.filename = utils_safe_strdup( file->filename );
      return 0;
    }
  }
  snapshot_cancel_pending();
  return error;
}

static int
snapshot_decision_matches( fuse_media_decision_id id )
{
  return id && id == pending_snapshot.id &&
         fuse_engine_pending_decision() == FUSE_DECISION_SNAPSHOT;
}

fuse_host_operation_result
fuse_host_answer_snapshot_joystick( fuse_media_decision_id id,
                                   const fuse_host_joystick *slot,
                                   fuse_open_request *next )
{
  int error;
  fuse_host_operation_result result;

  if( !next ) return FUSE_HOST_OPERATION_INVALID_ARGUMENT;
  *next = (fuse_open_request){ 0 };
  result = fuse_engine_check_operation();
  if( result != FUSE_HOST_OPERATION_OK ) return result;
  if( !snapshot_decision_matches( id ) ) return FUSE_HOST_OPERATION_STALE;
  if( slot && ( *slot < FUSE_HOST_JOYSTICK_1 || *slot > FUSE_HOST_JOYSTICK_KEYBOARD ) )
    return FUSE_HOST_OPERATION_INVALID_ARGUMENT;

  fuse_engine_begin_operation();
  if( slot ) pending_snapshot.outputs[*slot] = joystick_snapshot_type(
    libspectrum_snap_joystick_list( pending_snapshot.snap, pending_snapshot.index ) );
  pending_snapshot.index++;
  fuse_engine_retire_decision( FUSE_DECISION_SNAPSHOT );
  snapshot_next_joystick( next );
  if( !next->id ) {
    error = snapshot_restore( pending_snapshot.snap, pending_snapshot.outputs );
    if( pending_snapshot.source == SNAPSHOT_RZX_EXTERNAL ) {
      snapshot_cancel_pending();
      error = rzx_continue_requested_playback( next, error );
    } else {
      if( pending_snapshot.source == SNAPSHOT_RZX_EMBEDDED )
        error = rzx_finish_requested_playback( pending_snapshot.snap, error );
      else if( pending_snapshot.source == SNAPSHOT_RZX_RECORDING )
        error = rzx_finish_requested_recording( error );
      else if( pending_snapshot.source == SNAPSHOT_RZX_ROLLBACK )
        error = rzx_finish_rollback( error );
      else
        pokemem_find_pokfile( pending_snapshot.filename );
      snapshot_cancel_pending();
    }
    if( !next->id ) display_refresh_all();
    if( error ) result = FUSE_HOST_OPERATION_MEDIA_ERROR;
    else if( next->id ) result = FUSE_HOST_OPERATION_NEEDS_DECISION;
  } else {
    result = FUSE_HOST_OPERATION_NEEDS_DECISION;
  }
  fuse_engine_end_operation();
  return result;
}

int snapshot_write( const char *filename )
{
  libspectrum_id_t type;
  libspectrum_class_t class;
  libspectrum_snap *snap;
  unsigned char *buffer; size_t length;
  int flags;

  int error;

  /* Work out what sort of file we want from the filename; default to
     .szx if we couldn't guess */
  error = libspectrum_identify_file_with_class( &type, &class, filename, NULL,
						0 );
  if( error ) return error;

  if( class != LIBSPECTRUM_CLASS_SNAPSHOT || type == LIBSPECTRUM_ID_UNKNOWN )
    type = LIBSPECTRUM_ID_SNAPSHOT_SZX;

  snap = libspectrum_snap_alloc();

  error = snapshot_copy_to( snap );
  if( error ) { libspectrum_snap_free( snap ); return error; }

  flags = 0;
  length = 0;
  buffer = NULL;
  error = libspectrum_snap_write( &buffer, &length, &flags, snap, type,
				  fuse_creator, 0 );
  if( error ) {
    libspectrum_free( buffer );
    libspectrum_snap_free( snap );
    return error;
  }

  if( flags & LIBSPECTRUM_FLAG_SNAPSHOT_MAJOR_INFO_LOSS ) {
    ui_error(
      UI_ERROR_WARNING,
      "A large amount of information has been lost in conversion; the snapshot probably won't work"
    );
  } else if( flags & LIBSPECTRUM_FLAG_SNAPSHOT_MINOR_INFO_LOSS ) {
    ui_error(
      UI_ERROR_WARNING,
      "Some information has been lost in conversion; the snapshot may not work"
    );
  }

  error = libspectrum_snap_free( snap );
  if( error ) { libspectrum_free( buffer ); return 1; }

  error = utils_write_file( filename, buffer, length );
  if( error ) { libspectrum_free( buffer ); return error; }

  libspectrum_free( buffer );

  return 0;

}

int
snapshot_copy_to( libspectrum_snap *snap )
{
  libspectrum_snap_set_machine( snap, machine_current->machine );
  libspectrum_snap_set_late_timings( snap, settings_current.late_timings );

  module_snapshot_to( snap );

  return 0;
}
