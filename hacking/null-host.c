/* Bounded direct consumer of the private, selected-null-UI worker API.
   Not a toolkit-free core, command server or replacement for the legacy CLI.
   Build/run instructions and limits: hacking/legacy-engine.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "fuse-internal.h"
#include "debugger/debugger.h"
#include "keyboard_refresh.h"

typedef struct rom_provider {
  const char *directory;
  unsigned char *pending;
  unsigned int requests, releases;
  int queued_input, debugger_step, debugger_watchpoint;
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
  if( provider->debugger_watchpoint ) {
    static const unsigned char probe[16384] = {
      0x3e, 0x42,        /* LD A,42h */
      0x32, 0x00, 0x80,  /* LD (8000h),A */
      0x3e, 0x00,        /* LD A,0 */
      0x3a, 0x00, 0x80,  /* LD A,(8000h) */
      0x3c,              /* INC A */
      0xc3, 0, 0,        /* JP 0 */
    };
    if( strcmp( name, "48.rom" ) ) return 1;
    rom->data = probe;
    rom->length = sizeof( probe );
    rom->custom = 1;
    return 0;
  }
  if( provider->debugger_step ) {
    /* INC A; INC A; JP 0 */
    static const unsigned char probe[16384] = { 0x3c, 0x3c, 0xc3, 0, 0 };
    if( strcmp( name, "48.rom" ) ) return 1;
    rom->data = probe;
    rom->length = sizeof( probe );
    rom->custom = 1;
    return 0;
  }
  if( provider->queued_input ) {
    /* Read Space once. Halt if it is up; otherwise keep reading the row.
       A refresh on the next frame proves the first read saw the queued press. */
    static const unsigned char probe[16384] = {
      0xf3,                    /* DI */
      0x3e, 0x7f,              /* LD A,7Fh: select the Space row */
      0xdb, 0xfe,              /* IN A,(FEh) */
      0xe6, 0x01,              /* AND 1 */
      0x20, 0x06,              /* JR NZ,halt */
      0x3e, 0x7f, 0xdb, 0xfe,  /* loop: LD A,7Fh; IN A,(FEh) */
      0x18, 0xfa,              /* JR loop */
      0x76,                    /* halt: HALT */
    };
    if( strcmp( name, "48.rom" ) ) return 1;
    rom->data = probe;
    rom->length = sizeof( probe );
    rom->custom = 1;
    return 0;
  }
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

static fuse_host_operation_result
insert_clean_tape( const char *filename )
{
  fuse_media_decision_id request;
  return fuse_host_insert_tape_file( filename, &request );
}

static int
eject_saved_tape( void )
{
  fuse_media_decision_id request;
  return operation( "eject saved tape", fuse_host_request_tape_eject( &request ) );
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

typedef struct queued_keys {
  input_event_t events[2];
  unsigned int next, end;
} queued_keys;

/* The real host would fill this queue from its own event loop. The refresh
   callback runs on the engine thread and never pumps a toolkit or waits. */
static input_refresh_result
supply_queued_keys( void *context )
{
  queued_keys *queue = context;

  if( input_dispatch_pending() ) return INPUT_REFRESH_SKIPPED;
  while( queue->next < queue->end ) {
    input_dispatch_result result = input_event_dispatch(
      &queue->events[queue->next], INPUT_DISPATCH_READ_TIME );
    if( result == INPUT_DISPATCH_BLOCKED ) return INPUT_REFRESH_SKIPPED;
    if( result == INPUT_DISPATCH_ERROR ) return INPUT_REFRESH_FAILED;
    queue->next++;
    if( result == INPUT_DISPATCH_DEFERRED ) return INPUT_REFRESH_SKIPPED;
  }
  return INPUT_REFRESH_SUCCESS;
}

static int
queued_input_demo( unsigned int *completed )
{
  queued_keys queue = {
    {
      { .type = INPUT_EVENT_KEYPRESS, .types.key = { INPUT_KEY_space, INPUT_KEY_space } },
      { .type = INPUT_EVENT_KEYRELEASE, .types.key = { INPUT_KEY_space, INPUT_KEY_space } },
    },
    0, 1
  };
  int error = 1;

  keyboard_refresh_set_frontend( supply_queued_keys, &queue );
  if( frames( 1, completed ) || queue.next != 1 ) goto done;
  queue.end = 2; /* The host collects the release while Fuse is between turns. */
  if( frames( 1, completed ) || queue.next != 2 ) goto done;
  puts( "host: keyboard read saw queued Space press; next frame drained release" );
  error = 0;
done:
  keyboard_refresh_set_frontend( NULL, NULL );
  if( error ) fputs( "host: queued keyboard probe did not complete\n", stderr );
  return error;
}

static int
debugger_step_demo( unsigned int *budget_turns )
{
  processor before, after;
  fuse_debugger_views *views = NULL;

  if( operation( "request debugger break", fuse_host_debugger_break() ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      operation( "inspect manual break",
                 fuse_host_debugger_copy_registers( &before ) ) ||
      before.pc.w != 0 ||
      operation( "continue manual break",
                 fuse_host_debugger_continue() ) ) goto fail;
  puts( "host: manual debugger break at PC 0000" );

  if( operation( "break", fuse_host_debugger_break() ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      operation( "install breakpoint",
                 fuse_host_debugger_command( "break 0", NULL ) ) ||
      operation( "continue command",
                 fuse_host_debugger_command( "continue", NULL ) ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      operation( "inspect breakpoint", fuse_host_debugger_copy_registers( &before ) ) ||
      before.pc.w != 0 ) goto fail;
  puts( "host: debugger stopped at PC 0000" );
  if( operation( "copy debugger panes",
                 fuse_host_debugger_copy_views( &views ) ) ||
      views->breakpoint_count != 1 ||
      views->breakpoints[0].value.address.offset != 0 ||
      !views->page_count || !views->event_count ) goto fail;

  if( operation( "arm single-step", fuse_host_debugger_step() ) ||
      fuse_run_tstates( 1 ) != FUSE_RUN_COMPLETED ) goto fail;
  (*budget_turns)++;
  if( fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      operation( "inspect step", fuse_host_debugger_copy_registers( &after ) ) ||
      after.pc.w != 1 || ( after.af.w >> 8 ) != (unsigned char)( ( before.af.w >> 8 ) + 1 ) )
    goto fail;
  puts( "host: one instruction stepped; PC 0001 after budget boundary" );

  if( operation( "continue", fuse_host_debugger_continue() ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      operation( "inspect next breakpoint", fuse_host_debugger_copy_registers( &after ) ) ||
      after.pc.w != 0 || ( after.af.w >> 8 ) != (unsigned char)( ( before.af.w >> 8 ) + 2 ) )
    goto fail;
  puts( "host: Continue reached the same breakpoint again" );
  if( operation( "delete breakpoints",
                 fuse_host_debugger_command( "delete", NULL ) ) ||
      views->breakpoint_count != 1 ||
      views->breakpoints[0].type != DEBUGGER_BREAKPOINT_TYPE_EXECUTE ||
      !views->pages[0].source || !views->events[0].name ) goto fail;
  puts(
    "host: copied debugger panes survived Continue and breakpoint deletion" );
  fuse_debugger_views_free( views );
  return 0;

fail:
  fuse_debugger_views_free( views );
  fputs( "host: debugger step probe did not complete\n", stderr );
  return 1;
}

static int
watchpoint_demo( unsigned int *budget_turns )
{
  fuse_debugger_watchpoint watch;
  processor registers;

  if( debugger_breakpoint_add_address( DEBUGGER_BREAKPOINT_TYPE_WRITE,
        memory_source_any, 0, 0x8000, 0, DEBUGGER_BREAKPOINT_LIFE_PERMANENT, NULL ) ||
      debugger_breakpoint_add_address( DEBUGGER_BREAKPOINT_TYPE_READ,
        memory_source_any, 0, 0x8000, 0, DEBUGGER_BREAKPOINT_LIFE_PERMANENT, NULL ) ||
      debugger_breakpoint_add_address( DEBUGGER_BREAKPOINT_TYPE_EXECUTE,
        memory_source_any, 0, 10, 0, DEBUGGER_BREAKPOINT_LIFE_PERMANENT, NULL ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      fuse_host_debugger_copy_watchpoint( &watch ) != FUSE_HOST_OPERATION_OK ||
      watch.access != FUSE_DEBUGGER_ACCESS_WRITE || watch.address != 0x8000 ||
      fuse_host_debugger_copy_registers( &registers ) != FUSE_HOST_OPERATION_OK ||
      registers.pc.w != 5 ) goto fail;
  puts( "host: write watchpoint at 8000; stopped at PC 0005" );

  if( operation( "continue after write", fuse_host_debugger_continue() ) ||
      fuse_run_tstates( 20 ) != FUSE_RUN_COMPLETED ) goto fail;
  (*budget_turns)++;
  if( fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      fuse_host_debugger_copy_watchpoint( &watch ) != FUSE_HOST_OPERATION_OK ||
      watch.access != FUSE_DEBUGGER_ACCESS_READ || watch.address != 0x8000 ||
      fuse_host_debugger_copy_registers( &registers ) != FUSE_HOST_OPERATION_OK ||
      registers.pc.w != 10 || ( registers.af.w >> 8 ) != 0x42 ) goto fail;
  puts( "host: read watchpoint at 8000; stopped at PC 000a with A=42" );

  if( operation( "continue after read", fuse_host_debugger_continue() ) ||
      fuse_run_frame() != FUSE_RUN_DEBUGGER_STOPPED ||
      fuse_host_debugger_copy_watchpoint( &watch ) != FUSE_HOST_OPERATION_NOT_READY ||
      fuse_host_debugger_copy_registers( &registers ) != FUSE_HOST_OPERATION_OK ||
      registers.pc.w != 10 || ( registers.af.w >> 8 ) != 0x42 ) goto fail;
  puts( "host: Continue honored the execute breakpoint at PC 000a" );
  return 0;

fail:
  fputs( "host: debugger watchpoint probe did not complete\n", stderr );
  return 1;
}

/* A deterministic stand-in for commands from an independent UI loop. An idle
   worker would wait on its command queue here rather than spinning on ticks. */
static int
parked_eject_demo( const char *output, unsigned int *completed )
{
  enum { RECORD, TICK, EJECT, RELEASE_INPUT, CANCEL, FAIL_SAVE, SAVE, REOPEN,
         DISCARD };
  static const int commands[] = {
    RECORD, TICK, EJECT, RELEASE_INPUT, TICK, CANCEL, TICK,
    EJECT, TICK, FAIL_SAVE, EJECT, SAVE, REOPEN, EJECT,
    RECORD, TICK, EJECT, RECORD, TICK, EJECT, DISCARD
  };
  fuse_media_decision_id waiting = 0, retired = 0, request;
  fuse_host_operation_result result;
  fuse_tape_info *info = NULL;
  unsigned int parked_ticks = 0;
  size_t i;

  for( i = 0; i < sizeof( commands ) / sizeof( *commands ); i++ ) {
    switch( commands[i] ) {
    case TICK:
      if( waiting ) {
        /* Test the guard; a real worker would not issue these turns. */
        if( fuse_run_frame() != FUSE_RUN_NEEDS_DECISION ||
            fuse_run_tstates( 1000 ) != FUSE_RUN_NEEDS_DECISION ) goto fail;
        parked_ticks++;
      } else if( frames( 1, completed ) ) goto fail;
      break;
    case RELEASE_INPUT:
      if( !waiting || operation( "parked focus loss", fuse_host_release_all() ) )
        goto fail;
      break;
    case RECORD:
      if( operation( "record", fuse_host_start_tape_recording() ) ) goto fail;
      if( waiting ) {
        retired = waiting;
        waiting = 0;
        if( fuse_host_answer_tape_eject( retired, FUSE_MEDIA_DECISION_DISCARD,
                                        NULL ) != FUSE_HOST_OPERATION_STALE )
          goto fail;
      }
      break;
    case EJECT:
      result = fuse_host_request_tape_eject( &request );
      if( result == FUSE_HOST_OPERATION_OK && !request ) break;
      if( result != FUSE_HOST_OPERATION_NEEDS_DECISION || !request ||
          request == retired ) goto fail;
      waiting = request;
      if( fuse_host_request_tape_eject( &request ) != result ||
          request != waiting ) goto fail;
      if( retired && fuse_host_answer_tape_eject( retired,
            FUSE_MEDIA_DECISION_DISCARD, NULL ) != FUSE_HOST_OPERATION_STALE )
        goto fail;
      break;
    case REOPEN:
      if( operation( "reopen saved tape", insert_clean_tape( output ) ) ||
          fuse_host_answer_tape_eject( retired, FUSE_MEDIA_DECISION_DISCARD,
                                       NULL ) != FUSE_HOST_OPERATION_STALE )
        goto fail;
      break;
    case CANCEL:
    case FAIL_SAVE:
    case SAVE:
    case DISCARD:
      if( !waiting ) goto fail;
      result = fuse_host_answer_tape_eject( waiting,
        commands[i] == CANCEL ? FUSE_MEDIA_DECISION_CANCEL :
        commands[i] == DISCARD ? FUSE_MEDIA_DECISION_DISCARD : FUSE_MEDIA_DECISION_SAVE,
        commands[i] == FAIL_SAVE ? "missing-directory/recorded.pzx" :
        commands[i] == SAVE ? output : NULL );
      retired = waiting;
      waiting = 0;
      if( result != ( commands[i] == CANCEL ? FUSE_HOST_OPERATION_CANCELLED :
                      commands[i] == FAIL_SAVE ? FUSE_HOST_OPERATION_MEDIA_ERROR :
                                                   FUSE_HOST_OPERATION_OK ) )
        goto fail;
      if( fuse_host_answer_tape_eject( retired, FUSE_MEDIA_DECISION_DISCARD,
                                      NULL ) != FUSE_HOST_OPERATION_STALE )
        goto fail;
      if( operation( "eject observation", fuse_host_copy_tape_info( &info ) ) )
        goto fail;
      if( info->recording ||
          info->present != ( commands[i] == CANCEL || commands[i] == FAIL_SAVE ) ||
          info->modified != info->present ) goto fail;
      fuse_tape_info_free( info );
      info = NULL;
      break;
    }
  }
  if( waiting || parked_ticks != 2 || *completed != 4 ) goto fail;
  puts( "host: parked for eject; handled focus loss; checked cancel, save and stale replies" );
  return 0;

fail:
  fuse_tape_info_free( info );
  fputs( "host: parked tape eject scenario failed\n", stderr );
  return 1;
}

static int
replacement_demo( const char *input, const char *bad, unsigned int *completed )
{
  static const fuse_media_decision_choice choices[] = {
    FUSE_MEDIA_DECISION_CANCEL, FUSE_MEDIA_DECISION_SAVE, FUSE_MEDIA_DECISION_DISCARD
  };
  fuse_media_decision_id request, other;
  fuse_tape_info *info = NULL;
  size_t i;
  int failed = 1;

  if( operation( "record", fuse_host_start_tape_recording() ) ||
      frames( 1, completed ) ||
      operation( "stop recording", fuse_host_stop_tape_recording() ) ) goto done;
  if( fuse_host_insert_tape_file( "missing-directory/input.tap", &request ) !=
      FUSE_HOST_OPERATION_MEDIA_ERROR || request ) goto done;

  for( i = 0; i < sizeof( choices ) / sizeof( *choices ); i++ ) {
    const char *target = i == 1 ? "missing-directory/recorded.pzx" : NULL;
    if( fuse_host_insert_tape_file( input, &request ) !=
        FUSE_HOST_OPERATION_NEEDS_DECISION || !request ||
        fuse_run_frame() != FUSE_RUN_NEEDS_DECISION ||
        fuse_host_request_tape_eject( &other ) != FUSE_HOST_OPERATION_BUSY || other ||
        fuse_host_insert_tape_file( bad, &other ) != FUSE_HOST_OPERATION_BUSY || other )
      goto done;
    if( fuse_host_answer_tape_eject( request, choices[i], target ) !=
        ( i == 0 ? FUSE_HOST_OPERATION_CANCELLED :
          i == 1 ? FUSE_HOST_OPERATION_MEDIA_ERROR : FUSE_HOST_OPERATION_OK ) ) goto done;
    if( operation( "replacement observation", fuse_host_copy_tape_info( &info ) ) ||
        !info->present || info->recording || info->modified != ( i < 2 ) ) goto done;
    fuse_tape_info_free( info );
    info = NULL;
  }

  if( operation( "record again", fuse_host_start_tape_recording() ) ||
      frames( 1, completed ) ||
      fuse_host_insert_tape_file( bad, &request ) != FUSE_HOST_OPERATION_NEEDS_DECISION ||
      fuse_host_answer_tape_eject( request, FUSE_MEDIA_DECISION_DISCARD, NULL ) !=
        FUSE_HOST_OPERATION_MEDIA_ERROR ||
      operation( "failed replacement observation", fuse_host_copy_tape_info( &info ) ) ||
      info->present || info->modified ) goto done;

  puts( "host: tape replacement checked read, cancel, save and decode failures" );
  failed = 0;
done:
  fuse_tape_info_free( info );
  if( failed ) fputs( "host: tape replacement scenario failed\n", stderr );
  return failed;
}

static int
microdrive_demo( const char *filename )
{
  fuse_media_decision_id id;
  fuse_quit_request quit;
  fuse_microdrive_info *info;
  int retained;

  if( operation( "new cartridge", fuse_host_new_microdrive( 1, &id ) ) ||
      fuse_host_eject_microdrive( 1,
                                  &id ) != FUSE_HOST_OPERATION_NEEDS_DECISION ||
      fuse_run_frame() != FUSE_RUN_NEEDS_DECISION ||
      fuse_host_answer_microdrive_eject( id, FUSE_MEDIA_DECISION_CANCEL,
                                         NULL ) !=
      FUSE_HOST_OPERATION_CANCELLED ||
      operation( "observe cartridge",
                 fuse_host_copy_microdrive_info( 1, &info ) ) )
    return 1;
  retained = info->available && info->inserted && info->modified &&
             !info->filename;
  fuse_microdrive_info_free( info );
  if( !retained ||
      fuse_host_request_quit( &quit ) != FUSE_HOST_OPERATION_NEEDS_DECISION ||
      quit.media != FUSE_QUIT_MICRODRIVE || quit.slot != 1 ||
      quit.has_save_target ||
      fuse_run_frame() != FUSE_RUN_NEEDS_DECISION ||
      fuse_host_answer_quit( quit.id, FUSE_MEDIA_DECISION_SAVE, filename,
                             &quit ) !=
      FUSE_HOST_OPERATION_READY_TO_SHUT_DOWN ||
      fuse_run_frame() != FUSE_RUN_EXITING ) return 1;
  puts( "host: Microdrive 2 kept on Cancel and saved on Quit" );
  return 0;
}

static int
poke_finder_demo( const char *filename )
{
  fuse_poke_match *matches = NULL;
  fuse_pok_trainers *info = NULL;
  size_t count, bank, offset;
  libspectrum_byte original, changed, value;
  int error = 1;

  if( operation( "seed RAM", fuse_host_load_binary( filename, 0x8000, 1 ) ) ||
      operation( "read RAM", fuse_host_copy_memory( 0x8000, 1, &original ) ) ||
      operation( "reset search", fuse_host_reset_poke_finder() ) ||
      operation( "exact search",
                 fuse_host_filter_poke_finder( FUSE_POKE_EXACT, original ) ) )
    goto done;
  changed = original == 255 ? 254 : original + 1;
  if( operation( "add mapped trainer",
                 fuse_host_add_custom_pok_trainer( 8, 0x8000, changed ) ) ||
      operation( "copy trainers", fuse_host_copy_pok_trainers( &info ) ) ||
      info->count != 1 ||
      operation( "change RAM",
                 fuse_host_set_pok_trainer( info->generation, 0, 1, -1 ) ) ||
      operation( "filter search", fuse_host_filter_poke_finder(
                   original ==
                   255 ? FUSE_POKE_DECREMENTED : FUSE_POKE_INCREMENTED, 0 ) ) ||
      operation( "copy matches",
                 fuse_host_copy_poke_matches( &matches, &count ) ) ||
      count != 1 ||
      operation( "restore mapped byte",
                 fuse_host_set_pok_trainer( info->generation, 0, 0, -1 ) ) )
    goto done;
  bank = matches[0].bank;
  offset = matches[0].offset;
  if( bank >= 8 ||
      operation( "add matched trainer",
                 fuse_host_add_custom_pok_trainer( bank, offset, 256 ) ) ||
      operation( "apply matched trainer",
                 fuse_host_set_pok_trainer( info->generation, 1, 1,
                                            changed ) ) ||
      operation( "read matched byte",
                 fuse_host_copy_memory( 0x8000, 1, &value ) ) ||
      value != changed ||
      operation( "disable matched trainer",
                 fuse_host_set_pok_trainer( info->generation, 1, 0, -1 ) ) ||
      operation( "read restored byte",
                 fuse_host_copy_memory( 0x8000, 1, &value ) ) ||
      value != original ) goto done;
  printf(
    "host: Poke Finder matched bank %lu offset %04lx; trainer restored RAM\n",
    (unsigned long)bank, (unsigned long)offset );
  error = 0;

done:
  if( error ) fputs( "host: Poke Finder scenario failed\n", stderr );
  libspectrum_free( matches );
  fuse_pok_trainers_free( info );
  return error;
}

typedef struct diagnostic_capture {
  unsigned int errors;
  char message[256];
} diagnostic_capture;

static void
capture_diagnostic( void *context, fuse_diagnostic_level level,
                    const char *message )
{
  diagnostic_capture *capture = context;
  if( level != FUSE_DIAGNOSTIC_ERROR ) return;
  capture->errors++;
  snprintf( capture->message, sizeof( capture->message ), "%s", message );
}

static int
pok_demo( const char *filename )
{
  fuse_open_request request;
  fuse_pok_trainers *info;
  size_t i;
  int error = 1;

  if( operation( "open POK", fuse_host_open_file( filename, &request ) ) ||
      operation( "copy POK trainers", fuse_host_copy_pok_trainers( &info ) ) )
    return 1;
  for( i = 0; i < info->count; i++ ) {
    const fuse_pok_trainer *trainer = &info->trainers[i];
    if( trainer->disabled || trainer->active ) continue;
    if( operation( "enable trainer", fuse_host_set_pok_trainer(
                     info->generation, i, 1,
                     trainer->asks_value ? 42 : -1 ) ) ||
        operation( "disable trainer", fuse_host_set_pok_trainer(
                     info->generation, i, 0, -1 ) ) ) break;
    printf( "host: enabled and disabled POK trainer %s\n", trainer->name );
    error = 0;
    break;
  }
  if( i == info->count ) fputs( "host: no inactive, usable POK trainer\n",
                                stderr );
  fuse_pok_trainers_free( info );
  return error;
}

static int
open_demo( const char *filename, const char *movie_target,
           unsigned int *completed )
{
  const fuse_host_joystick keyboard = FUSE_HOST_JOYSTICK_KEYBOARD;
  fuse_open_request request;
  fuse_host_operation_result result = movie_target ?
                                      fuse_host_record_movie_from_rzx( filename,
                                                                       movie_target,
                                                                       &request )
  :
                                      fuse_host_open_file( filename, &request );
  unsigned int mappings = 0;

  while( result == FUSE_HOST_OPERATION_NEEDS_DECISION ) {
    if( !request.id || fuse_run_frame() != FUSE_RUN_NEEDS_DECISION ) {
      fputs( "host: Open decision did not park execution\n", stderr );
      return 1;
    }
    switch( request.question ) {
    case FUSE_OPEN_SAVE_TAPE:
      puts( "host: modified tape; discard" );
      result = fuse_host_answer_tape_eject( request.id,
                                            FUSE_MEDIA_DECISION_DISCARD, NULL );
      break;
    case FUSE_OPEN_SAVE_FLOPPY:
      printf( "host: modified floppy controller=%d slot=%d; %s; discard\n",
              (int)request.detail.floppy.controller, request.detail.floppy.slot,
              request.detail.floppy.has_save_target ? "existing save target" :
              "Save needs a path" );
      result = fuse_host_answer_disk_eject( request.id,
                                            FUSE_MEDIA_DECISION_DISCARD, NULL,
                                            &request );
      break;
    case FUSE_OPEN_MERGE_FLOPPY:
      puts( "host: floppy side merge; accept" );
      result = fuse_host_answer_disk_merge( request.id, 1 );
      break;
    case FUSE_OPEN_SAVE_MASS_STORAGE:
      printf( "host: modified IDE/MMC controller=%d unit=%d; discard\n",
              (int)request.detail.mass_storage.controller,
              (int)request.detail.mass_storage.unit );
      result = fuse_host_answer_mass_storage_eject( request.id,
                                                    FUSE_MEDIA_DECISION_DISCARD );
      break;
    case FUSE_OPEN_RZX_EXTERNAL_SNAPSHOT:
      puts( "host: RZX needs an external snapshot; cancel replay" );
      result = fuse_host_answer_rzx_external_snapshot( request.id, NULL,
                                                       &request );
      return result != FUSE_HOST_OPERATION_CANCELLED;
    case FUSE_OPEN_MAP_JOYSTICK:
      printf( "host: snapshot joystick %s; %s\n",
              libspectrum_joystick_name( request.detail.joystick.type ),
              mappings ? "decline mapping" : "map to keyboard" );
      result = fuse_host_answer_snapshot_joystick( request.id,
                                                   mappings ? NULL : &keyboard,
                                                   &request );
      mappings++;
      break;
    default:
      fputs( "host: unknown Open question\n", stderr );
      return 1;
    }
  }
  if( operation( "File Open", result ) ) return 1;
  puts( mappings ? "host: snapshot restored after joystick decision" :
        "host: File Open returned legacy success" );
  return frames( 5, completed );
}

static int
movie_session( const char *path, unsigned int *completed )
{
  fuse_movie_state state;
  if( operation( "start movie", fuse_host_start_movie( path ) ) ||
      frames( 1, completed ) ||
      operation( "pause movie", fuse_host_toggle_movie_pause() ) ||
      operation( "inspect paused movie",
                 fuse_host_copy_movie_state( &state ) ) ||
      state != FUSE_MOVIE_PAUSED || frames( 1, completed ) ||
      operation( "resume movie", fuse_host_toggle_movie_pause() ) ||
      operation( "inspect resumed movie",
                 fuse_host_copy_movie_state( &state ) ) ||
      state != FUSE_MOVIE_RECORDING || frames( 1, completed ) ||
      operation( "stop movie", fuse_host_stop_movie() ) ||
      operation( "inspect stopped movie",
                 fuse_host_copy_movie_state( &state ) ) ||
      state != FUSE_MOVIE_IDLE ) return 1;
  puts( "host: recorded, paused, resumed and finalized live FMF" );
  return 0;
}

static int
svg_session( const char *name, const char *root, unsigned int *completed )
{
  fuse_svg_capture_info info;
  svg_capture_type mode;
  if( !strcmp( name, "lines" ) ) mode = SVG_CAPTURE_LINES;
  else if( !strcmp( name, "dots" ) ) mode = SVG_CAPTURE_DOTS;
  else return 1;
  if( operation( "query SVG capture",
                 fuse_host_copy_svg_capture_info( &info ) ) )
    return 1;
  if( !info.available ) {
    puts( "host: SVG capture unavailable" );
    return 0;
  }
  if( operation( "start SVG capture",
                 fuse_host_start_svg_capture( root, mode ) ) ||
      operation( "inspect SVG capture",
                 fuse_host_copy_svg_capture_info( &info ) ) ||
      !info.active || info.mode != mode || frames( 1, completed ) ||
      operation( "stop SVG capture", fuse_host_stop_svg_capture() ) ||
      operation( "inspect SVG capture",
                 fuse_host_copy_svg_capture_info( &info ) ) ||
      info.active ) return 1;
  printf( "host: saved SVG %s capture\n", name );
  return 0;
}

static int
screenshot_session( const char *name, const char *output,
                    unsigned int *completed )
{
  fuse_screenshot_format format;
  const char *scaler = NULL;

  if( !strcmp( name, "scr" ) ) format = FUSE_SCREENSHOT_SCR;
  else if( !strcmp( name, "mlt" ) ) format = FUSE_SCREENSHOT_MLT;
  else if( !strcmp( name, "png" ) ) {
    format = FUSE_SCREENSHOT_PNG;
    if( operation( "query PNG scaler", fuse_host_png_scaler_id( 0, &scaler ) ) )
      return 1;
    if( !scaler ) {
      puts( "host: PNG screenshots unavailable" );
      return 0;
    }
  } else return 1;

  if( frames( 1, completed ) ||
      operation( "save screenshot",
                 fuse_host_save_screenshot( format, output, scaler ) ) )
    return 1;
  printf( "host: saved %s screenshot\n", name );
  return 0;
}

static int
machine_button_session( const char *name, const char *snapshot )
{
  fuse_machine_button button;
  fuse_open_request request;
  fuse_host_operation_result result;
  int available;

  if( !strcmp( name, "multiface" ) ) button = FUSE_MACHINE_BUTTON_MULTIFACE_RED;
  else if( !strcmp( name,
                    "disciple" ) ) button = FUSE_MACHINE_BUTTON_DISCIPLE_MAGIC;
  else if( !strcmp( name,
                    "didaktik" ) ) button = FUSE_MACHINE_BUTTON_DIDAKTIK_SNAP;
  else return 1;

  result = fuse_host_open_file( snapshot, &request );
  while( result == FUSE_HOST_OPERATION_NEEDS_DECISION &&
         request.question == FUSE_OPEN_MAP_JOYSTICK )
    result = fuse_host_answer_snapshot_joystick( request.id, NULL, &request );
  if( operation( "open button snapshot", result ) ||
      operation( "inspect machine button",
                 fuse_host_machine_button_available( button, &available ) ) ||
      !available ||
      operation( "press machine button",
                 fuse_host_press_machine_button( button ) ) ||
      fuse_run_tstates( 1 ) != FUSE_RUN_COMPLETED ) return 1;
  printf( "host: queued and ran %s button\n", name );
  return 0;
}

static int
rs232_session( const char *rx, const char *tx )
{
  fuse_rs232_info info;
#ifdef WIN32
  (void)tx;
  if( fuse_host_copy_rs232_info( FUSE_RS232_RX, &info ) !=
      FUSE_HOST_OPERATION_OK || info.available || info.connected ||
      fuse_host_connect_rs232( FUSE_RS232_RX, rx ) !=
      FUSE_HOST_OPERATION_MEDIA_ERROR )
    return 1;
  puts( "host: RS232 unavailable on Win32" );
#else
  if( operation( "connect RS232 RX",
                 fuse_host_connect_rs232( FUSE_RS232_RX, rx ) ) ||
      operation( "connect RS232 TX",
                 fuse_host_connect_rs232( FUSE_RS232_TX, tx ) ) ||
      operation( "inspect RS232 RX",
                 fuse_host_copy_rs232_info( FUSE_RS232_RX, &info ) ) ||
      !info.available || !info.connected ||
      operation( "disconnect RS232 RX",
                 fuse_host_disconnect_rs232( FUSE_RS232_RX ) ) ||
      operation( "inspect RS232 RX",
                 fuse_host_copy_rs232_info( FUSE_RS232_RX, &info ) ) ||
      info.connected ||
      operation( "inspect RS232 TX",
                 fuse_host_copy_rs232_info( FUSE_RS232_TX, &info ) ) ||
      !info.connected ||
      operation( "disconnect RS232 TX",
                 fuse_host_disconnect_rs232( FUSE_RS232_TX ) ) ||
      operation( "inspect RS232 TX",
                 fuse_host_copy_rs232_info( FUSE_RS232_TX, &info ) ) ||
      info.connected ) return 1;
  puts( "host: RS232 RX and TX connected and disconnected independently" );
#endif
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
  fuse_media_decision_id disk_request;
  fuse_run_result run;
  unsigned int frame_turns = 0, budget_turns = 0;
  int prepared = 0, status = EXIT_FAILURE;
  int queued_input = argc == 3 && !strcmp( argv[2], "--queued-input" );
  int debugger_step = argc == 3 && !strcmp( argv[2], "--debugger-step" );
  int debugger_watchpoint = argc == 3 && !strcmp( argv[2], "--debugger-watchpoint" );
  int disk_lifecycle = argc == 4 && !strcmp( argv[2], "--disk-lifecycle" );
  int tape_record = argc == 4 && !strcmp( argv[2], "--record-tape" );
  int parked_eject = argc == 4 && !strcmp( argv[2], "--parked-tape-eject" );
  int snapshot_save = argc == 4 && !strcmp( argv[2], "--save-snapshot" );
  int filesystem = argc == 4 && !strcmp( argv[2], "--filesystem" );
  int divmmc = argc == 4 && !strcmp( argv[2], "--divmmc" );
  int filesystem_installed = 0;
  memory_filesystem memory = { 0 };
  int disk_save = argc == 5 && !strcmp( argv[2], "--save-disk" );
  int tape_lifecycle = argc == 5 && !strcmp( argv[2], "--tape-lifecycle" );
  int tape_replacement = argc == 5 && !strcmp( argv[2], "--replace-tape" );
  int diagnostics = argc >= 3 && !strcmp( argv[2], "--diagnostics" );
  diagnostic_capture capture = { 0 };
  int open_pok = argc >= 3 && !strcmp( argv[2], "--open-pok" );
  int poke_finder = argc >= 3 && !strcmp( argv[2], "--poke-finder" );
  int binary = argc >= 3 && !strcmp( argv[2], "--binary-roundtrip" );
  int rzx_movie = argc >= 3 && !strcmp( argv[2], "--rzx-to-movie" );
  int rzx_record = argc >= 3 && !strcmp( argv[2], "--record-rzx" );
  int microdrive = argc >= 3 && !strcmp( argv[2], "--microdrive" );
  int rs232 = argc >= 3 && !strcmp( argv[2], "--rs232" );
  int machine_button = argc >= 3 && !strcmp( argv[2], "--machine-button" );
  int save_screen = argc >= 3 && !strcmp( argv[2], "--save-screen" );
  int capture_svg = argc >= 3 && !strcmp( argv[2], "--capture-svg" );
  int record_movie = argc >= 3 && !strcmp( argv[2], "--record-movie" );
  int open_over_tape = argc >= 3 && !strcmp( argv[2], "--open-over-tape" );
  int open_over_floppy = argc >= 3 && !strcmp( argv[2], "--open-over-floppy" );
  int plus3 = disk_save || disk_lifecycle || open_over_floppy;

  if( ( argc != 2 && argc != 3 && argc != 4 && argc != 5 ) ||
      ( ( microdrive || rzx_record || open_over_tape || open_over_floppy ||
          open_pok || poke_finder || diagnostics || record_movie ) &&
        argc != 4 ) ||
      ( argc == 4 && strcmp( argv[2],
                             "--open" ) && !open_over_tape &&
        !open_over_floppy && !open_pok && !poke_finder && !diagnostics &&
        !microdrive && !record_movie &&
        !disk_lifecycle &&
        !tape_record &&
        !rzx_record &&
        !parked_eject &&
        !snapshot_save && !filesystem && !divmmc ) ||
      ( ( rzx_movie || binary || rs232 || machine_button || save_screen || capture_svg ) && argc != 5 ) ||
      ( argc == 5 && !disk_save && !tape_lifecycle && !tape_replacement && !rzx_movie && !binary && !rs232 && !machine_button && !save_screen && !capture_svg ) ||
      ( argc == 3 && ( !strcmp( argv[2], "--open" ) || !strcmp( argv[2], "--save-disk" ) ||
                      !strcmp( argv[2], "--disk-lifecycle" ) || !strcmp( argv[2], "--tape-lifecycle" ) || !strcmp( argv[2], "--replace-tape" ) ||
                      !strcmp( argv[2], "--record-tape" ) || !strcmp( argv[2], "--parked-tape-eject" ) || !strcmp( argv[2], "--save-snapshot" ) || !strcmp( argv[2], "--filesystem" ) || !strcmp( argv[2], "--divmmc" ) ) ) ) {
    fprintf( stderr, "Usage: %s ROM_DIRECTORY [TAPE_FILE | --open FILE | --diagnostics MISSING_FILE | --open-pok FILE | --binary-roundtrip INPUT OUTPUT | --poke-finder INPUT | --open-over-tape FILE | --open-over-floppy FILE | --save-disk INPUT OUTPUT | --disk-lifecycle OUTPUT | --tape-lifecycle INPUT OUTPUT | --record-tape OUTPUT | --parked-tape-eject OUTPUT | --replace-tape INPUT BAD_INPUT | --save-snapshot OUTPUT | --filesystem TAPE_FILE | --divmmc CARD_FILE | --microdrive OUTPUT | --rs232 RX TX | --machine-button NAME SNAPSHOT | --save-screen FORMAT OUTPUT | --capture-svg MODE ROOT | --record-movie OUTPUT | --record-rzx OUTPUT | --rzx-to-movie INPUT OUTPUT | --queued-input | --debugger-step | --debugger-watchpoint]\n", argv[0] );
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
  provider.queued_input = queued_input;
  provider.debugger_step = debugger_step;
  provider.debugger_watchpoint = debugger_watchpoint;
  provider.directory = argv[1]; /* Borrowed through shutdown, not passed as engine CLI. */
  config = fuse_host_config_alloc();
  if( fuse_host_config_set_machine( config, plus3 ? "plus3" : "48" ) ||
      fuse_host_config_set_sound( config, 0 ) ||
      ( diagnostics && fuse_host_config_set_diagnostics( config,
                                                         capture_diagnostic,
                                                         &capture ) ) ||
      ( ( tape_record || parked_eject || tape_replacement || open_over_tape ) &&
        fuse_host_config_set_tape_traps( config, 0 ) ) ||
      ( filesystem && fuse_host_config_set_auto_load( config, 0 ) ) ||
      ( divmmc && fuse_host_config_set_divmmc( config, 1 ) ) ||
      ( ( microdrive || rs232 ) && fuse_host_config_set_interface1( config, 1 ) ) ||
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
  puts( plus3 ? "host: prepared plus3" : "host: prepared 48" );
  if( record_movie ) {
    if( !movie_session( argv[3], &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( capture_svg ) {
    if( !svg_session( argv[3], argv[4], &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( save_screen ) {
    if( !screenshot_session( argv[3], argv[4],
                             &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( machine_button ) {
    if( !machine_button_session( argv[3], argv[4] ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( rs232 ) {
    if( !rs232_session( argv[3], argv[4] ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( diagnostics ) {
    fuse_open_request request;
    capture.errors = 0;
    if( fuse_host_open_file( argv[3],
                             &request ) != FUSE_HOST_OPERATION_MEDIA_ERROR ||
        request.id || !capture.errors ) {
      fputs( "host: expected a diagnostic for the failed Open\n", stderr );
      goto cleanup;
    }
    printf( "host: retained Open diagnostic: %s\n", capture.message );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( poke_finder ) {
    if( !poke_finder_demo( argv[3] ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( binary ) {
    libspectrum_byte bytes[4];
    if( operation( "load binary",
                   fuse_host_load_binary( argv[3], 0x8000,
                                          sizeof( bytes ) ) ) ||
        operation( "copy memory",
                   fuse_host_copy_memory( 0x8000, sizeof( bytes ), bytes ) ) ||
        operation( "save binary",
                   fuse_host_save_binary( argv[4], 0x8000, sizeof( bytes ) ) ) )
      goto cleanup;
    printf( "host: RAM at 8000: %02x %02x %02x %02x\n",
            (unsigned)bytes[0], (unsigned)bytes[1],
            (unsigned)bytes[2], (unsigned)bytes[3] );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( open_pok ) {
    if( !pok_demo( argv[3] ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( rzx_movie ) {
    if( !open_demo( argv[3], argv[4], &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( rzx_record ) {
    fuse_open_request request;
    if( operation( "record RZX",
                   fuse_host_start_rzx_recording( argv[3], 1 ) ) ||
        frames( 1, &frame_turns ) ||
        operation( "insert RZX snapshot", fuse_host_insert_rzx_snapshot() ) ||
        frames( 1, &frame_turns ) ||
        operation( "roll back RZX", fuse_host_rollback_rzx( &request ) ) ||
        frames( 1, &frame_turns ) ||
        operation( "stop recording", fuse_host_stop_rzx() ) ||
        operation( "continue RZX",
                   fuse_host_continue_rzx_recording( argv[3], &request ) ) ||
        frames( 1, &frame_turns ) ||
        operation( "stop continued recording", fuse_host_stop_rzx() ) ||
        operation( "finalise RZX", fuse_host_finalise_rzx( argv[3] ) ) ||
        operation( "replay RZX", fuse_host_play_rzx( argv[3], &request ) ) ||
        operation( "stop replay", fuse_host_stop_rzx() ) ) goto cleanup;
    puts( "host: recorded, finalised and reopened RZX" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( microdrive ) {
    if( !microdrive_demo( argv[3] ) ) status = EXIT_SUCCESS;
    else fputs( "host: Microdrive scenario failed\n", stderr );
    goto cleanup;
  }
  if( tape_replacement ) {
    if( !replacement_demo( argv[3], argv[4], &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( parked_eject ) {
    if( !parked_eject_demo( argv[3], &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( debugger_watchpoint ) {
    if( !watchpoint_demo( &budget_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( debugger_step ) {
    if( !debugger_step_demo( &budget_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( queued_input ) {
    if( !queued_input_demo( &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( divmmc ) {
    fuse_media_decision_id id;
    fuse_mass_storage_info *drive = NULL;
    int mounted;
    if( operation( "insert DivMMC", fuse_host_insert_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, "media/card.hdf", &id ) ) ||
        operation( "observe DivMMC", fuse_host_copy_mass_storage_info( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, &drive ) ) ) goto cleanup;
    mounted = drive->active && drive->backing_name && !strcmp( drive->backing_name, "media/card.hdf" );
    if( !mounted || operation( "commit DivMMC", fuse_host_commit_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER ) ) ||
        operation( "eject DivMMC", fuse_host_eject_mass_storage( PERIPH_TYPE_DIVMMC, LIBSPECTRUM_IDE_MASTER, &id ) ) ) {
      fuse_mass_storage_info_free( drive );
      goto cleanup;
    }
    fuse_mass_storage_info_free( drive );
    puts( "host: DivMMC lifecycle; handles balanced" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( filesystem ) {
    if( operation( "filesystem Tape Open", fuse_host_open_file( "media/input.tap", &(fuse_open_request){ 0 } ) ) ||
        operation( "filesystem tape save", fuse_host_save_tape_file( "media/copy.pzx" ) ) ||
        eject_saved_tape() ||
        operation( "filesystem tape reopen",
                   fuse_host_open_file( "media/copy.pzx",
                                        &(fuse_open_request){ 0 } ) ) ||
        frames( 2, &frame_turns ) ||
        operation( "filesystem snapshot save", fuse_host_save_snapshot_file( "media/state.szx" ) ) ||
        operation( "reset", fuse_host_reset( FUSE_HOST_RESET_HARD ) ) ||
        operation( "filesystem snapshot reopen",
                   fuse_host_open_file( "media/state.szx",
                                        &(fuse_open_request){ 0 } ) ) ||
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
          operation( "open ZXCF", fuse_host_open_file( "media/drive.hdf", &(fuse_open_request){ 0 } ) ) ||
          operation( "observe ZXCF", fuse_host_copy_mass_storage_info( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &drive ) ) )
        goto cleanup;
      ok = drive->active && drive->backing_name && !strcmp( drive->backing_name, "media/drive.hdf" );
      fuse_mass_storage_info_free( drive );
      if( !ok || operation( "commit ZXCF", fuse_host_commit_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER ) ) ||
          operation( "eject ZXCF", fuse_host_eject_mass_storage( PERIPH_TYPE_ZXCF, LIBSPECTRUM_IDE_MASTER, &disk_request ) ) ) goto cleanup;
    }
    puts( "host: effective filesystem; tape/snapshot roundtrip; ZXCF lifecycle; handles balanced" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( snapshot_save ) {
    if( frames( 2, &frame_turns ) ||
        operation( "save snapshot", fuse_host_save_snapshot_file( argv[3] ) ) ||
        operation( "reset", fuse_host_reset( FUSE_HOST_RESET_HARD ) ) ||
        operation( "reopen snapshot",
                   fuse_host_open_file( argv[3],
                                        &(fuse_open_request){ 0 } ) ) ||
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
        eject_saved_tape() ) goto cleanup;
    print_tape_info( "saved recorded tape copy", saved_tape_info );
    puts( "host: recorded two frames; saved and ejected tape" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( tape_lifecycle ) {
    if( operation( "tape insertion", insert_clean_tape( argv[3] ) ) ||
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
        eject_saved_tape() ||
        operation( "empty tape info", fuse_host_copy_tape_info( &tape_info ) ) ) goto cleanup;
    print_tape_info( "saved tape copy", saved_tape_info );
    print_tape_info( "ejected tape copy", tape_info );
    puts( "host: tape deck controls; saved and ejected" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( disk_save ) {
    fuse_open_request question;
    fuse_host_operation_result result = fuse_host_insert_disk_file(
      FUSE_DISK_CONTROLLER_PLUS3, 0, argv[3], &question );
    if( result == FUSE_HOST_OPERATION_NEEDS_DECISION &&
        question.question == FUSE_OPEN_MERGE_FLOPPY )
      result = fuse_host_answer_disk_merge( question.id, 1 );
    if( operation( "disk insertion", result ) ||
        operation( "disk save", fuse_host_save_disk_file( FUSE_DISK_CONTROLLER_PLUS3, 0, argv[4] ) ) ) goto cleanup;
    puts( "host: saved disk through plus3 drive 0" );
    status = EXIT_SUCCESS;
    goto cleanup; /* Core codecs handle both input and output; no CPU turn needed. */
  }
  if( disk_lifecycle ) {
    if( operation( "new disk", fuse_host_new_disk( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk_request ) ) ||
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
        operation( "disk eject", fuse_host_eject_disk( disk->controller, disk->slot, &disk_request ) ) ) goto cleanup;
    fuse_disk_drive_info_free( disk ); disk = NULL;
    if( operation( "empty drive info", fuse_host_copy_disk_drive_info( FUSE_DISK_CONTROLLER_PLUS3, 0, &disk ) ) ) goto cleanup;
    print_disk_info( "ejected drive copy", disk );
    puts( "host: new disk; flip/WP controls; saved and ejected plus3 drive 0" );
    status = EXIT_SUCCESS;
    goto cleanup;
  }
  if( argc == 4 ) {
    if( open_over_tape &&
        ( operation( "record", fuse_host_start_tape_recording() ) ||
          frames( 1, &frame_turns ) ||
          operation( "stop recording",
                     fuse_host_stop_tape_recording() ) ) ) goto cleanup;
    if( open_over_floppy &&
        operation( "new floppy", fuse_host_new_disk( FUSE_DISK_CONTROLLER_PLUS3,
                                                     0,
                                                     &disk_request ) ) ) goto
      cleanup;
    if( !open_demo( argv[3], NULL, &frame_turns ) ) status = EXIT_SUCCESS;
    goto cleanup; /* Neither dispatch success nor five turns prove loading complete. */
  }
  if( argc == 3 ) {
    if( operation( "tape insertion", insert_clean_tape( argv[2] ) ) ) goto cleanup;
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
