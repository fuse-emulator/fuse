/* soundpartitiontest.c: Bit-exact frame-local audio partition equivalence
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/* Exercise physical admission even in a build using a non-FIFO backend. */
#ifndef SOUND_PCM_ADMISSION
#define SOUND_PCM_ADMISSION 1
#endif
#include "sound.c"
/* Include the implementations to inspect private synthesis/filter state and
   retain the preceding AY loop as an independent frame-only oracle. No test
   entry points, scheduler, or state-observation hooks enter production. */
#include "sound/ay_engine.c"
#include "sound/source_synths.c"
#include "sound/output_mixer.c"
#include "peripherals/sound/sp0256.c"
#include "sound/pcm_fifo.h"

#define CHECK(x) do { if( !(x) ) { \
  fprintf( stderr, "soundpartitiontest:%d: %s (case %d/%d/%d frame %d)\n", \
           __LINE__, #x, test_stereo, test_route, test_sources, test_frame ); \
  exit( 1 ); } } while( 0 )
#define CAPACITY 4096
#define FRAMES 12

static fuse_machine_info test_machine;
fuse_machine_info *machine_current = &test_machine;
settings_info settings_current;
int movie_recording = 1, tape_microphone;
libspectrum_dword tstates;
static int test_stereo, test_route, test_sources, test_frame;
static sfifo_t fifo;
static int speaker, selected_stereo, deny_admission, legacy;
static unsigned int width, reserved, reserve_calls, output_calls, movie_calls;
static libspectrum_dword preview_elapsed;
static blip_sample_t device_pcm[CAPACITY], movie_pcm[CAPACITY];
static int device_count, movie_count;
static blip_sample_t expected_pcm[FRAMES][CAPACITY];
static blip_sample_t expected_raw[FRAMES][CAPACITY];
static int expected_count[FRAMES], expected_raw_count[FRAMES];
static uint64_t expected_state[FRAMES];
static unsigned long zero_intervals, total_intervals, total_frames;
static unsigned long total_pcm_frames;

int periph_is_active( periph_type type )
{
  return type == PERIPH_TYPE_SPECDRUM || type == PERIPH_TYPE_COVOX_FB;
}
void startup_manager_register( startup_manager_module module,
  startup_manager_module *dependencies, size_t count,
  startup_manager_init_fn init, void *context, startup_manager_end_fn end )
{
  (void)module; (void)dependencies; (void)count;
  (void)init; (void)context; (void)end;
}
int option_enumerate_sound_stereo_ay( void ) { return selected_stereo; }
int option_enumerate_sound_speaker_type( void ) { return speaker; }
int timer_fastloading_active( void ) { return 0; }
int tape_is_playing( void ) { return 0; }
int ui_error( ui_error_level level, const char *message, ... )
{ (void)level; (void)message; return 0; }
void fuse_abort( void ) { abort(); }
void movie_init_sound( int frequency, int stereo )
{ (void)frequency; (void)stereo; }
void movie_add_sound( libspectrum_signed_word *data, int count )
{
  CHECK( ++movie_calls == 1 && count <= CAPACITY );
  movie_count = count;
  memcpy( movie_pcm, data, count * sizeof( *data ) );
}
int sound_lowlevel_init( const char *device, int *frequency, int *stereo )
{
  (void)device; (void)frequency;
  width = *stereo ? 4 : 2;
  return sfifo_init( &fifo, 32767 );
}
void sound_lowlevel_end( void ) { sfifo_close( &fifo ); }
int sound_lowlevel_reserve( unsigned int frames )
{
  CHECK( frames > 0 );
  {
    long preview = blip_buffer_samples_after( left_buf, preview_elapsed );
    long remaining = sound_framesiz - frame_sample_count / sound_channels;
    if( preview > remaining ) preview = remaining;
    CHECK( frames == preview );
  }
  if( deny_admission ) {
    CHECK( pcm_fifo_producer_can_write( &fifo, width, frames ) == 0 );
    return -EAGAIN;
  }
  CHECK( pcm_fifo_producer_can_write( &fifo, width, frames ) == 1 );
  reserved = frames;
  reserve_calls++;
  return 0;
}
void sound_lowlevel_frame( libspectrum_signed_word *data, int count )
{
  blip_sample_t readback[CAPACITY];
  int frames = count / sound_channels;
  CHECK( frames == (int)reserved && frames > 0 );
  CHECK( pcm_fifo_write( &fifo, width, data, frames ) == frames );
  CHECK( pcm_fifo_read( &fifo, width, readback, frames ) == frames );
  CHECK( !memcmp( readback, data, count * sizeof( *data ) ) );
  CHECK( device_count + count <= CAPACITY );
  memcpy( device_pcm + device_count, data, count * sizeof( *data ) );
  device_count += count;
  reserved = 0;
  output_calls++;
}

/* The preceding frame-only AY renderer, including its zero-based tick lattice
   and frame-local comparison. Do not turn this into calls to resumable render. */
static void
legacy_ay_render( libspectrum_dword end )
{
  struct ay_change *change = changes;
  int changes_left = change_count;
  int old_previous[AY_CHANNELS] = { 0, 0, 0 };
  libspectrum_dword f;
  if( !( machine_current->capabilities & LIBSPECTRUM_MACHINE_CAPABILITY_AY ) )
    return;
  for( f = 0; f < end; f += AY_CLOCK_DIVISOR * AY_CLOCK_RATIO ) {
    unsigned int tone_count;
    int noise_count, channel, envelope_level;
    while( changes_left && f >= change->tstates ) {
      ay_apply_change( change++ );
      changes_left--;
    }
    envelope_level = levels[env_counter];
    noise_count = ay_clock_envelope( registers[13] );
    tone_cycles += AY_CLOCK_DIVISOR;
    tone_count = tone_cycles >> 3;
    tone_cycles &= 7;
    for( channel = 0; channel < AY_CHANNELS; channel++ )
      ay_emit_channel( channel, f,
        ay_channel_output( channel, envelope_level, tone_count ),
        &old_previous[channel] );
    ay_clock_noise( noise_count );
  }
}

static void
legacy_frame( void )
{
  long count;
  libspectrum_dword end = test_machine.timings.tstates_per_frame;
  reserved = blip_buffer_samples_after( left_buf, end );
  if( reserved > (unsigned int)sound_framesiz ) reserved = sound_framesiz;
  sound_update_source_routes();
  sp0256_do_frame();
  legacy_ay_render( end );
  blip_buffer_end_frame( left_buf, end );
  if( sound_channels == 2 ) {
    blip_buffer_end_frame( right_buf, end );
    count = blip_buffer_read_samples( left_buf, samples, sound_framesiz, 1 );
    blip_buffer_read_samples( right_buf, samples + 1, count, 1 );
    count *= 2;
  } else {
    count = blip_buffer_read_samples( left_buf, samples, sound_framesiz, 0 );
  }
  /* The mixer loop itself is unchanged; a single call followed by publication
     exposes exactly the old extraction buffer and count. */
  output_mixer_advance( end, samples, count );
  output_mixer_end_frame();
  sound_lowlevel_frame( samples, count );
  movie_add_sound( samples, count );
  ay_engine_end_frame();
}

static uint64_t
hash_bytes( uint64_t hash, const void *data, size_t count )
{
  const unsigned char *p = data;
  while( count-- ) { hash ^= *p++; hash *= UINT64_C(1099511628211); }
  return hash;
}
#define HASH(x) hash = hash_bytes( hash, &(x), sizeof( x ) )
static uint64_t
buffer_state( uint64_t hash, Blip_Buffer *buffer )
{
  if( !buffer ) return hash;
  hash = hash_bytes( hash, &buffer->offset_, sizeof( buffer->offset_ ) );
  hash = hash_bytes( hash, &buffer->reader_accum, sizeof( buffer->reader_accum ) );
  /* At safe cuts all delivered events are at or before the endpoint. After
     extraction only fractional residue and this impulse tail remain live. */
  return hash_bytes( hash, buffer->buffer_,
                     ( blip_buffer_samples_avail( buffer ) +
                       BLIP_WIDEST_IMPULSE_ + 2 ) *
                     sizeof( *buffer->buffer_ ) );
}
static uint64_t
state( void )
{
  uint64_t hash = UINT64_C(14695981039346656037);
  HASH( registers ); HASH( tone_tick ); HASH( tone_high ); HASH( tone_period );
  HASH( noise_period ); HASH( env_period ); HASH( noise_tick );
  HASH( tone_cycles ); HASH( env_cycles ); HASH( env_internal_tick );
  HASH( env_tick ); HASH( rng ); HASH( noise_toggle ); HASH( env_first );
  HASH( env_reverse ); HASH( env_counter ); HASH( change_count );
  HASH( change_position ); HASH( next_tick ); HASH( previous );
  HASH( unfiltered_dc_filter ); HASH( beeper_filter ); HASH( tv_filters );
  HASH( ula_filter ); HASH( audio_position ); HASH( frame_sample_count );
  HASH( ula_frame_count ); HASH( ula_published_count );
  /* Scratch ring cursors describe batching, not speech state: each run
     discards the old scratch contents. Compare the LPC state and sequencer. */
  HASH( sp0256.filt ); HASH( sp0256.sound_current );
  HASH( sp0256.pc ); HASH( sp0256.ald ); HASH( sp0256.halted );
  HASH( sp0256.lrq ); HASH( sp0256.mode ); HASH( sp0256.page );
  HASH( sp0256.silent ); HASH( sp0256.fifo_head ); HASH( sp0256.fifo_tail );
  HASH( sp0256.fifo_bitp );
  {
    uint64_t left = buffer_state( 0, left_buf );
    uint64_t right = buffer_state( 0, right_buf );
    uint64_t ula = buffer_state( 0, ula_buf );
    uint64_t tv_left = buffer_state( 0, tv_left_buf );
    uint64_t tv_right = buffer_state( 0, tv_right_buf );
    HASH( left ); HASH( right ); HASH( ula ); HASH( tv_left ); HASH( tv_right );
  }
  for( int channel = 0; channel < AY_CHANNELS; channel++ ) {
    HASH( synths[channel]->impl.last_amp );
    if( right_synths[channel] ) HASH( right_synths[channel]->impl.last_amp );
  }
  HASH( ula_synth->impl.last_amp );
  HASH( left_sp0256->impl.last_amp );
  HASH( left_covox->impl.last_amp ); HASH( left_specdrum->impl.last_amp );
  return hash;
}

static const blip_sample_t *
raw_output( int *count )
{
  if( speaker == SOUND_SPEAKER_TYPE_BEEPER ) {
    *count = sound_ula_beeper_output_count();
    CHECK( !sound_ula_mic_output_count() && !sound_ula_mic_output() );
    return sound_ula_beeper_output();
  }
  *count = sound_ula_mic_output_count();
  CHECK( !sound_ula_beeper_output_count() && !sound_ula_beeper_output() );
  return sound_ula_mic_output();
}

static void
advance( libspectrum_dword endpoint )
{
  const int old_calls = reserve_calls;
  int raw_count;
  const blip_sample_t *raw = raw_output( &raw_count );
  CHECK( raw_count <= CAPACITY );
  const int old_count = raw_count;
  blip_sample_t saved[CAPACITY];
  memcpy( saved, raw, raw_count * sizeof( *raw ) );
  preview_elapsed = endpoint - audio_position;
  long upcoming = blip_buffer_samples_after( left_buf, preview_elapsed );
  if( upcoming > sound_framesiz - frame_sample_count / sound_channels )
    upcoming = sound_framesiz - frame_sample_count / sound_channels;
  const uint64_t before = state();
  CHECK( sound_advance_to( audio_position ) == 0 && state() == before );
  CHECK( sound_advance_to( test_machine.timings.tstates_per_frame + 1 ) < 0 );
  CHECK( state() == before );
  if( audio_position ) {
    CHECK( sound_advance_to( audio_position - 1 ) < 0 );
    CHECK( state() == before );
  }
  if( upcoming && ( total_intervals % 127 == 0 ||
                    endpoint == 34944 || endpoint == 69888 ) ) {
    blip_sample_t queued[16384] = { 0 };
    int capacity = pcm_fifo_capacity( &fifo, width );
    CHECK( pcm_fifo_write( &fifo, width, queued, capacity ) == capacity );
    deny_admission = 1;
    CHECK( sound_advance_to( endpoint ) < 0 );
    deny_admission = 0;
    CHECK( state() == before && !movie_calls );
    CHECK( pcm_fifo_consumer_used( &fifo, width ) == capacity );
    CHECK( pcm_fifo_read( &fifo, width, queued, capacity ) == capacity );
  }
  CHECK( sound_advance_to( endpoint ) == 0 );
  CHECK( reserve_calls == old_calls + !!upcoming );
  CHECK( !movie_calls && frame_sample_count == device_count );
  CHECK( raw == raw_output( &raw_count ) && raw_count == old_count );
  CHECK( !memcmp( saved, raw, raw_count * sizeof( *raw ) ) );
  CHECK( audio_position == endpoint );
  CHECK( blip_buffer_samples_avail( left_buf ) >= 0 );
  zero_intervals += !upcoming;
  total_intervals++;
}

static void
sources_at( libspectrum_dword time )
{
  int f = test_frame;
  tstates = time;
  if( test_sources & 1 ) sound_ula( time, ( time / 97 + f ) & 1,
                                   ( time / 131 + f ) & 1 );
  if( test_sources & 2 ) {
    sound_ay_write( 0, 17 + ( time + f ) % 29, time );
    sound_ay_write( 1, 0, time );
    sound_ay_write( 6, 7, time );
    sound_ay_write( 7, 0x30, time );
    sound_ay_write( 8, 12, time );
    sound_ay_write( 9, 16, time );
    sound_ay_write( 11, 3, time );
    sound_ay_write( 13, ( f + time / 500 ) & 15, time );
  }
  if( test_sources & 8 ) {
    sound_specdrum_write( 0, ( time + f ) & 255 );
    sound_covox_write( 0, ( time / 13 + f ) & 255 );
  }
}

static void
initialise( void )
{
  static libspectrum_byte rom[4096];
  settings_current.sound = 1;
  sound_init( NULL );
  CHECK( sound_enabled && audio_position == 0 && !frame_sample_count );
  /* Existing AY lifecycle intentionally retains some oscillator state.
     Restore the same initial machine state for each independent comparison. */
  rng = 1; noise_toggle = 0; env_first = 1; env_reverse = 0;
  env_counter = AY_ENV_STEPS - 1;
  sound_ula( 0, 0, 0 );
  sound_ay_reset();
  if( test_sources & 4 ) {
    CHECK( sp0256_init( rom ) == 0 );
    /* Real LPC synthesis, both periodic and noise excitation, with enough
       repeats to span the fixture. No ROM file or real device is needed. */
    sp0256.silent = 0;
    sp0256.filt.rpt = 100000;
    sp0256.filt.per = test_route == 2 ? 0 : 17;
    sp0256.filt.amp = 128;
    sp0256.filt.b_coef[0] = -80;
    sp0256.filt.f_coef[0] = 100;
  }
}

/* Discontinuities are not compared with an unreset frame. Both runs reset
   at the same committed endpoint, then vary only the partitioning around it. */
static void
test_discontinuities( void )
{
  uint64_t saved_state = 0;
  blip_sample_t saved_pcm[CAPACITY], saved_raw[CAPACITY];
  int saved_count = 0, saved_raw_count = 0;
  test_sources = 7;
  test_route = 2;
  speaker = SOUND_SPEAKER_TYPE_TV;
  selected_stereo = SOUND_STEREO_AY_ABC;
  test_machine.capabilities |= LIBSPECTRUM_MACHINE_CAPABILITY_AY;
  test_frame = 0;
  legacy = 0;
  for( int pass = 0; pass < 2; pass++ ) {
    initialise();
    device_count = movie_calls = movie_count = 0;
    reserve_calls = output_calls = reserved = 0;
    sources_at( 0 );
    if( pass ) { advance( 1 ); advance( 31 ); advance( 999 ); }
    advance( 1000 );
    sound_ay_reset();
    CHECK( audio_position == 1000 && !movie_calls );
    int raw_count;
    const blip_sample_t *raw = raw_output( &raw_count );
    CHECK( !raw_count );
    sources_at( 1001 );
    if( pass ) { advance( 1001 ); advance( 1002 ); advance( 2001 ); }
    preview_elapsed = 69888 - audio_position;
    sound_frame();
    raw = raw_output( &raw_count );
    CHECK( movie_calls == 1 && device_count == movie_count );
    CHECK( !memcmp( movie_pcm, device_pcm, device_count * sizeof( *device_pcm ) ) );
    if( !pass ) {
      saved_count = device_count;
      saved_raw_count = raw_count;
      memcpy( saved_pcm, device_pcm, device_count * sizeof( *device_pcm ) );
      memcpy( saved_raw, raw, raw_count * sizeof( *raw ) );
      saved_state = state();
    } else {
      CHECK( saved_count == device_count && saved_raw_count == raw_count );
      CHECK( !memcmp( saved_pcm, device_pcm, device_count * sizeof( *device_pcm ) ) );
      CHECK( !memcmp( saved_raw, raw, raw_count * sizeof( *raw ) ) );
      CHECK( saved_state == state() );
    }
    movie_calls = device_count = movie_count = 0;
    sources_at( 0 );
    advance( 1000 );
    sound_pause();
    CHECK( !sound_enabled && !audio_position && !frame_sample_count );
    CHECK( !movie_calls );
    sp0256_end();
    sound_unpause();
    CHECK( sound_enabled && !audio_position && !frame_sample_count );
    raw_output( &raw_count );
    CHECK( !raw_count );
    preview_elapsed = 69888;
    sound_frame();
    CHECK( movie_calls == 1 );
    sound_end();
  }
}

int
main( int argc, char **argv )
{
  static const int source_cases[] = { 1, 2, 3, 4, 7, 15 };
  static const libspectrum_dword events[] = {
    0, 31, 32, 33, 73, 349, 350, 351, 500, 501, 502, 997,
    12345, 34943, 34944, 34945, 60001, 69886, 69887
  };
  test_machine.timings.processor_speed = 3500000;
  test_machine.timings.tstates_per_frame = 69888;
  settings_current.emulation_speed = 100;
  settings_current.sound_freq = argc > 1 ? atoi( argv[1] ) : 48000;
  CHECK( settings_current.sound_freq >= 8000 && settings_current.sound_freq <= 96000 );
  settings_current.volume_beeper = settings_current.volume_ay = 70;
  settings_current.volume_specdrum = settings_current.volume_covox = 50;
  settings_current.volume_uspeech = 50;
  /* Passes: preceding frame-only oracle, new one-shot path, halves,
     irregular event-adjacent cuts, 79-tstate cuts ending explicitly at F,
     and dense one-tstate cuts. Repeated frames exercise residue and rebase.
     At 96 kHz Blip rounding also exercises the shared frame extraction cap. */
  for( test_stereo = 0; test_stereo < 3; test_stereo++ ) {
    selected_stereo = test_stereo == 0 ? SOUND_STEREO_AY_NONE :
                      test_stereo == 1 ? SOUND_STEREO_AY_ABC : SOUND_STEREO_AY_ACB;
    for( test_route = 0; test_route < 3; test_route++ ) {
      speaker = test_route == 0 ? SOUND_SPEAKER_TYPE_UNFILTERED :
                test_route == 1 ? SOUND_SPEAKER_TYPE_BEEPER : SOUND_SPEAKER_TYPE_TV;
      for( int source = 0; source < 6; source++ ) {
        test_sources = source_cases[source];
        test_machine.capabilities = LIBSPECTRUM_MACHINE_CAPABILITY_BEEPER |
          ( test_sources & 2 ? LIBSPECTRUM_MACHINE_CAPABILITY_AY : 0 );
        for( int pass = 0; pass < 6; pass++ ) {
          legacy = pass == 0;
          initialise();
          for( test_frame = 0; test_frame < FRAMES; test_frame++ ) {
            libspectrum_dword position = 0;
            device_count = movie_count = movie_calls = 0;
            reserve_calls = output_calls = reserved = 0;
            const int pattern = pass;
            for( int event = 0;
                 event < (int)( sizeof( events ) / sizeof( *events ) );
                 event++ ) {
              const libspectrum_dword at = events[event];
              if( pattern == 2 && position < 34944 && at > 34944 ) {
                position = 34944;
                advance( position );
              }
              if( pattern >= 3 ) {
                const libspectrum_dword step = pattern == 5 ?
                  ( position < 1100 || position > 69800 ? 1 : 791 ) :
                  pattern == 4 ? 79 : 1379;
                while( position + step < at ) {
                  position += step;
                  advance( position );
                }
                if( at && position < at - 1 ) {
                  position = at - 1;
                  advance( position );
                }
              }
              sources_at( at );
              if( pattern >= 3 && position < at ) {
                position = at;
                advance( position );
              }
            }
            if( legacy ) legacy_frame();
            else {
              /* Sources are delivered in chronological machine time. Cuts
                 never precede already delivered ULA/DAC events. AY may still
                 have queued writes beyond its next tick. */
              if( pattern == 4 ) advance( 69888 );
              preview_elapsed = 69888 - audio_position;
              sound_frame();
            }
            CHECK( movie_calls == 1 && movie_count == device_count );
            CHECK( !memcmp( movie_pcm, device_pcm,
                             movie_count * sizeof( *movie_pcm ) ) );
            CHECK( audio_position == 0 && !frame_sample_count );
            int raw_count;
            const blip_sample_t *raw = raw_output( &raw_count );
            if( legacy ) {
              expected_count[test_frame] = device_count;
              expected_raw_count[test_frame] = raw_count;
              memcpy( expected_pcm[test_frame], device_pcm,
                      device_count * sizeof( *device_pcm ) );
              memcpy( expected_raw[test_frame], raw,
                      raw_count * sizeof( *raw ) );
              expected_state[test_frame] = state();
            } else {
              CHECK( device_count == expected_count[test_frame] );
              CHECK( !memcmp( expected_pcm[test_frame], device_pcm,
                               device_count * sizeof( *device_pcm ) ) );
              CHECK( raw_count == expected_raw_count[test_frame] );
              CHECK( !memcmp( expected_raw[test_frame], raw,
                               raw_count * sizeof( *raw ) ) );
              CHECK( state() == expected_state[test_frame] );
            }
            total_frames++;
            total_pcm_frames += device_count / sound_channels;
          }
          sp0256_end();
          sound_end();
          CHECK( !sound_enabled && !audio_position && !frame_sample_count );
        }
      }
    }
  }
  test_discontinuities();
  printf( "Partition equivalence at %d Hz: %lu frames, %lu cuts, "
          "%lu zero-sample cuts, %lu PCM frames; PCM/raw/movie/state identical\n",
          settings_current.sound_freq, total_frames, total_intervals,
          zero_intervals, total_pcm_frames );
  return 0;
}
