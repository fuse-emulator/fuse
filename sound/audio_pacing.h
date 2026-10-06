/* Shared callback audio pacing, GPL-2.0-or-later.
   FIFO geometry/payload ordering remains in pcm_fifo. The callback publishes
   only SC activity, progress and sticky faults; all policy is producer-owned. */
#ifndef FUSE_AUDIO_PACING_H
#define FUSE_AUDIO_PACING_H
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include "sound.h"
#include "audio_controller.h"
#include "pcm_fifo.h"

struct audio_cut { unsigned int active; uint32_t progress; };
struct audio_pacing {
  struct adaptive_controller controller;
  _Atomic unsigned int active;
  _Atomic uint32_t progress;
  _Atomic bool missing, invalid;
  unsigned int demand; /* Immutable qualified callback request envelope. */
  unsigned int startup_bound, batch;
  uint64_t transaction_epoch;
  uint32_t baseline;
  bool ready, normal, reserved, ordinary, compatible, quarantine;
};

/* Quiescent initialization only. D is the backend's qualified request bound,
   not a relaxed high-water of observed callbacks. Envelope violations latch. */
static inline int
audio_pacing_init( struct audio_pacing *p, unsigned int capacity,
                   unsigned int allowance, unsigned int demand,
                   unsigned int startup_bound )
{
  memset( p, 0, sizeof( *p ) );
  if( !capacity || !allowance || allowance > capacity ) return -EINVAL;
  if( demand ) {
    if( adaptive_init( &p->controller, capacity, allowance, demand ) )
      return -EINVAL;
  } else {
    /* Unknown request envelope is terminal for adaptation, not PCM playback.
       Do not invent a demand estimate or calculate qualified geometry. */
    p->controller.capacity = capacity;
    p->controller.allowance = allowance;
    p->controller.target = p->controller.accepted =
      p->controller.recovery = capacity;
    p->controller.state = ADAPTIVE_INVALID;
    adaptive_invalidate( &p->controller );
  }
  atomic_init( &p->active, 0 ); atomic_init( &p->progress, 0 );
  atomic_init( &p->missing, false ); atomic_init( &p->invalid, !demand );
  if( !atomic_is_lock_free( &p->active ) ||
      !atomic_is_lock_free( &p->progress ) ||
      !atomic_is_lock_free( &p->missing ) ||
      !atomic_is_lock_free( &p->invalid ) ) return -ENOTSUP;
  p->demand = demand; p->startup_bound = startup_bound;
  p->quarantine = p->ready = true;
  return 0;
}
/* B_ord is a cadence policy allowance, not an instruction-production bound.
   Use the same whole-frame rounding as synthesis; delayed B may exceed it. */
static inline int
audio_pacing_init_rate( struct audio_pacing *p, sfifo_t *fifo,
                        unsigned int width, unsigned int frequency,
                        unsigned int processor_speed, unsigned int frame_time,
                        unsigned int demand )
{
  libspectrum_dword interval;
  libspectrum_qword allowance;
  double startup;
  int capacity = pcm_fifo_capacity( fifo, width );
  if( capacity < 0 ) return capacity;
  if( !processor_speed || !frequency || !frame_time ) return -EINVAL;
  interval = sound_audio_interval_tstates( processor_speed );
  if( !interval ) return -EINVAL;
  allowance = (libspectrum_qword)frequency * interval / processor_speed;
  startup = (double)frequency * frame_time / processor_speed;
  if( allowance >= UINT_MAX || !isfinite( startup ) || startup >= UINT_MAX )
    return -EINVAL;
  return audio_pacing_init( p, capacity, (unsigned int)allowance + 1,
                           demand, (unsigned int)startup + 1 );
}
static inline void
audio_pacing_callback_begin( struct audio_pacing *p )
{
  if( p->ready ) atomic_store( &p->active, 1 );
}
static inline void
audio_pacing_callback_end( struct audio_pacing *p, unsigned int requested,
                           unsigned int delivered, bool invalid )
{
  bool notify = delivered != 0;
  if( !p->ready ) return;
  if( delivered < requested && !atomic_exchange( &p->missing, true ) )
    notify = true;
  if( (invalid || requested > p->demand) &&
      !atomic_exchange( &p->invalid, true ) ) notify = true;
  if( notify ) atomic_fetch_add( &p->progress, 1 );
  atomic_store( &p->active, 0 );
}
static inline struct audio_cut
audio_pacing_cut( struct audio_pacing *p )
{
  struct audio_cut cut;
  /* Both endpoints: active MUST be loaded before progress. */
  cut.active = atomic_load( &p->active );
  cut.progress = atomic_load( &p->progress );
  return cut;
}
static inline void
audio_pacing_evidence( struct audio_pacing *p, struct audio_cut before,
                       unsigned int q, unsigned int written,
                       struct audio_cut after, bool missing, bool invalid,
                       bool started, bool ordinary, bool complete )
{
  struct adaptive_controller *s = &p->controller;
  enum publication_evidence kind;
  struct adaptive_sample sample;
  bool contaminated = before.active || after.active ||
                      before.progress != after.progress;
  uint64_t bound = p->normal ? s->allowance : p->startup_bound;
  uint64_t structure = bound + p->demand - 1;
  uint64_t reserve = structure < s->target ? s->target - structure : 0;
  unsigned int old_target = s->target;
  uint64_t old_epoch = s->epoch;

  if( missing ) kind = PUBLICATION_UNDERRUN;
  else if( invalid || !started ) kind = PUBLICATION_INVALID;
  else if( q <= p->demand ) kind = PUBLICATION_WARNING;
  else if( contaminated ) kind = PUBLICATION_AMBIGUOUS;
  else if( structure + p->demand > s->target ) kind = PUBLICATION_INVALID;
  else if( reserve <= p->demand ) kind = PUBLICATION_WARNING;
  else if( !written || !complete || (p->normal && !ordinary) ||
           q > s->target || (uint64_t)q + written > s->target ||
           (p->normal && (uint64_t)q + written + bound <= s->target) ||
           (uint64_t)q < (uint64_t)p->demand + 2 ||
           reserve < (uint64_t)p->demand + 2 ) kind = PUBLICATION_NONE;
  else if( p->quarantine ) kind = PUBLICATION_BASELINE;
  else if( before.progress != p->baseline ) kind = PUBLICATION_CLEAN;
  else kind = PUBLICATION_NONE;

  if( kind == PUBLICATION_BASELINE || kind == PUBLICATION_CLEAN ) {
    p->baseline = after.progress; p->quarantine = false;
  } else if( kind != PUBLICATION_NONE ) p->quarantine = true;

  /* Startup qualification uses the conservative whole-frame geometry, excludes
     its magnitudes, and requires started output plus actual consumer progress. */
  if( !p->normal && p->compatible && kind == PUBLICATION_BASELINE &&
      before.progress && complete ) {
    p->normal = true; p->quarantine = true;
    adaptive_activity( s );
    return;
  }
  sample = (struct adaptive_sample){ kind, q, q + written, after.progress,
    p->reserved ? p->transaction_epoch : s->epoch, ordinary, complete,
    written > 0, !contaminated, invalid };
  if( kind == PUBLICATION_BASELINE && ordinary && complete && !contaminated ) {
    uint64_t epoch = s->epoch;
    adaptive_activity( s );
    if( s->epoch != epoch ) sample.epoch = s->epoch;
  }
  /* Geometry-only INVALID at LIMITED is not a terminal envelope failure.
     Known fallback is not ordinary evidence. */
  if( kind == PUBLICATION_INVALID && !invalid &&
      s->state != ADAPTIVE_STARTUP ) sample.kind = PUBLICATION_NONE;
  adaptive_observe( s, sample );
  if( old_target != s->target || old_epoch != s->epoch ) p->quarantine = true;
}

static inline int
audio_pacing_occupancy( sfifo_t *fifo, unsigned int width )
{
  int space = pcm_fifo_producer_space( fifo, width );
  return space < 0 ? space : pcm_fifo_capacity( fifo, width ) - space;
}

/* Called once before a positive exact production batch, BEFORE source mutation.
   Exceptional pre-refill observations retain old funded evidence; their size
   itself never supplies an ordinary reserve requirement. */
static inline int
audio_pacing_pending( struct audio_pacing *p, sfifo_t *fifo,
                      unsigned int width, unsigned int frames, bool compatible,
                      bool started )
{
  struct audio_cut before, after;
  int q;
  bool missing, invalid;
  if( !p->ready ) return 0;
  if( p->reserved || frames > p->controller.capacity ||
      pcm_fifo_capacity( fifo, width ) != (int)p->controller.capacity )
    return -EINVAL;
  if( !frames ) return 0;
  p->compatible = compatible && !atomic_load( &p->invalid );
  p->ordinary = p->normal && p->compatible &&
                frames <= p->controller.allowance;
  if( !p->ordinary ) {
    if( p->normal ) {
      missing = atomic_exchange( &p->missing, false );
      invalid = atomic_load( &p->invalid );
      before = audio_pacing_cut( p );
      q = audio_pacing_occupancy( fifo, width );
      after = audio_pacing_cut( p );
      missing |= atomic_exchange( &p->missing, false );
      invalid |= atomic_load( &p->invalid );
      if( q < 0 ) return q;
      audio_pacing_evidence( p, before, q, 0, after, missing, invalid,
                             started, true, false );
    }
    if( atomic_load( &p->invalid ) ) {
      struct adaptive_sample fault = { .kind = PUBLICATION_INVALID,
                                      .terminal_invalid = true };
      adaptive_observe( &p->controller, fault );
    } else if( !p->compatible ) {
      if( p->controller.state != ADAPTIVE_STARTUP )
        adaptive_fallback( &p->controller );
    } else if( p->normal ) adaptive_exceptional( &p->controller );
    p->quarantine = true;
  }
  p->batch = frames;
  p->transaction_epoch = p->controller.epoch;
  return 0;
}
static inline int
audio_pacing_can_admit( struct audio_pacing *p, sfifo_t *fifo,
                        unsigned int width, unsigned int frames )
{
  int physical = pcm_fifo_producer_can_write( fifo, width, frames );
  int q;
  unsigned int limit;
  if( physical <= 0 || !p->ready || !frames ) return physical;
  if( p->batch != frames ) return -EINVAL;
  limit = p->ordinary ? p->controller.target : p->controller.capacity;
  q = audio_pacing_occupancy( fifo, width );
  if( q < 0 ) return q;
  if( frames > limit ) return -EINVAL;
  return (unsigned int)q <= limit - frames;
}
/* Whole production was physically reserved; sole consumer progress can only
   increase its available space. Thus publication is one indivisible PCM piece,
   unlike the experimental backend's legacy partial-write fallback. */
static inline int
audio_pacing_write( struct audio_pacing *p, sfifo_t *fifo, unsigned int width,
                    const void *data, unsigned int frames, bool started )
{
  struct audio_cut before, after;
  int q, written;
  bool missing, invalid;
  if( !p->ready || !p->reserved )
    return pcm_fifo_write( fifo, width, data, frames );
  if( frames != p->batch ) return -EINVAL;
  missing = atomic_exchange( &p->missing, false );
  invalid = atomic_load( &p->invalid );
  before = audio_pacing_cut( p );
  q = audio_pacing_occupancy( fifo, width );
  if( q < 0 ) return q;
  if( (unsigned int)q > p->controller.capacity - frames ) return -EINVAL;
  written = pcm_fifo_write( fifo, width, data, frames );
  after = audio_pacing_cut( p );
  missing |= atomic_exchange( &p->missing, false );
  invalid |= atomic_load( &p->invalid );
  if( written != (int)frames ) return written < 0 ? written : -EINVAL;
  audio_pacing_evidence( p, before, q, frames, after, missing, invalid,
                         started, p->ordinary, true );
  p->reserved = false;
  if( !p->ordinary ) p->quarantine = true;
  if( adaptive_probe( &p->controller ) ) p->quarantine = true;
  return written;
}
#endif
