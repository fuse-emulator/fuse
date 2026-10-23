/* wdfdctest.c: standalone tests for the WD FDC emulation */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "libspectrum.h"

#include "bitmap.h"
#include "event.h"
#include "machine.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/wd_fdc.h"

/*
 * Functions supplied by wdfdc_fakes.c.
 */
void wdfdc_test_machine_init( void );
void wdfdc_test_reset_events( void );
int wdfdc_test_run_next_event( void );
int wdfdc_test_event_count( const char *description );
libspectrum_dword wdfdc_test_event_time( const char *description );


static int step_callback_count;
static int step_callback_direction;

static int intrq_set_count;
static int intrq_reset_count;
static int datarq_set_count;
static int datarq_reset_count;

static void prepare_multisector_read( wd_fdc *fdc, fdd_t *drive,
                                      libspectrum_byte sector );


/*
 * Reset the signal callback counters.
 */
static void
reset_signal_counters( void )
{
  intrq_set_count = 0;
  intrq_reset_count = 0;
  datarq_set_count = 0;
  datarq_reset_count = 0;
}


/*
 * Test callbacks for the WD FDC output signals.
 *
 * A normal Fuse peripheral installs callbacks for these signals.
 * The standalone harness installs its own callbacks so tests can verify
 * not only the internal FDC state but also the externally visible signal
 * transitions.
 */
static void
test_set_intrq( wd_fdc *fdc )
{
  (void)fdc;

  intrq_set_count++;
}


static void
test_reset_intrq( wd_fdc *fdc )
{
  (void)fdc;

  intrq_reset_count++;
}


static void
test_set_datarq( wd_fdc *fdc )
{
  (void)fdc;

  datarq_set_count++;
}


static void
test_reset_datarq( wd_fdc *fdc )
{
  (void)fdc;

  datarq_reset_count++;
}


/*
 * Test callback used to verify the optional WD FDC step callback.
 */
static void
test_step_callback( int direction )
{
  step_callback_count++;
  step_callback_direction = direction;
}


/*
 * Initialise only the FDD state required by these tests.
 */
static void
init_test_drive( fdd_t *drive, int cylinder )
{
  memset( drive, 0, sizeof( *drive ) );

  drive->type = FDD_SHUGART;
  drive->fdd_heads = 2;
  drive->fdd_cylinders = 80;
  drive->c_cylinder = cylinder;
  drive->tr00 = cylinder == 0;
}


/*
 * Allocate a WD1770 connected to the supplied drive.
 *
 * Motor is marked as already running so commands do not enter the WD1770
 * spin-up delay. Motor timing is not under test here.
 *
 * A real Fuse peripheral installs handlers for INTRQ and DRQ after
 * allocating the FDC. The standalone harness installs test handlers here.
 */
static wd_fdc *
init_test_fdc_flags( fdd_t *drive, unsigned int flags )
{
  wd_fdc *fdc;

  fdc = wd_fdc_alloc_fdc( WD1770, 0, flags );

  fdc->set_intrq = test_set_intrq;
  fdc->reset_intrq = test_reset_intrq;
  fdc->set_datarq = test_set_datarq;
  fdc->reset_datarq = test_reset_datarq;

  fdc->current_drive = drive;
  fdc->status_register |= WD_FDC_SR_MOTORON;

  return fdc;
}


static wd_fdc *
init_test_fdc( fdd_t *drive )
{
  return init_test_fdc_flags( drive, WD_FLAG_NONE );
}


/*
 * Dispose of an FDC used by a test.
 */
static void
free_test_fdc( wd_fdc *fdc )
{
  wdfdc_test_reset_events();
  libspectrum_free( fdc );
}


/*
 * Prepare an FDC so one data-register write completes a multisector record.
 *
 * This deliberately bypasses sector search and disk-image setup. The code
 * under test is the sector-completion logic in wd_fdc_dr_write().
 */
static void
prepare_multisector_write( wd_fdc *fdc, libspectrum_byte sector )
{
  fdc->command_register = 0xb0;
  fdc->state = WD_FDC_STATE_WRITE;
  fdc->status_type = WD_FDC_STATUS_TYPE2;
  fdc->status_register |= WD_FDC_SR_BUSY;

  fdc->sector_register = sector;
  fdc->sector_increment_pending = 0;

  fdc->data_multisector = 1;
  fdc->sector_length = 1;
  fdc->data_offset = 0;

  fdc->crc = 0xffff;
}


/*
 * Prepare an FDC so one data-register read completes a multisector record.
 * The unloaded artificial drive leaves its data byte unchanged, allowing
 * this test to exercise the real READ completion path without a disk image.
 */
static void
prepare_multisector_read( wd_fdc *fdc, fdd_t *drive,
                          libspectrum_byte sector )
{
  fdc->command_register = 0x90;
  fdc->state = WD_FDC_STATE_READ;
  fdc->status_type = WD_FDC_STATUS_TYPE2;
  fdc->status_register |= WD_FDC_SR_BUSY;

  fdc->sector_register = sector;
  fdc->sector_increment_pending = 0;
  fdc->data_multisector = 1;
  fdc->sector_length = 1;
  fdc->data_offset = 0;

  drive->data = 0;
  fdc->crc = 0;
}


/*
 * Prepare an active Type II command for Force Interrupt tests.
 *
 * Several Type II status bits are deliberately set so the tests can
 * distinguish historical Type I conversion from preserved Type II status.
 */
static void
prepare_force_interrupt( wd_fdc *fdc )
{
  fdc->command_register = 0x80;
  fdc->state = WD_FDC_STATE_READ;
  fdc->status_type = WD_FDC_STATUS_TYPE2;

  fdc->status_register =
    WD_FDC_SR_MOTORON |
    WD_FDC_SR_BUSY |
    WD_FDC_SR_WRPROT |
    WD_FDC_SR_RNF |
    WD_FDC_SR_CRCERR |
    WD_FDC_SR_LOST |
    WD_FDC_SR_IDX_DRQ;

  /*
   * The status bit and internal DRQ state are both asserted so
   * Force Interrupt must perform a real DRQ transition.
   */
  fdc->datarq = 1;

  fdc->intrq = 0;

  fdc->read_id = 1;
  fdc->id_mark = WD_FDC_AM_ID;
  fdc->sector_increment_pending = 1;
}


/*
 * Verify the original Fuse STEP IN behavior.
 */
static int
wd_fdc_standard_step_in_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc( &drive );

  wd_fdc_cr_write( fdc, 0x40 );

  if( drive.c_cylinder != 11 ) {
    printf(
      "wd_fdc_standard_step_in_test: cylinder is %d, expected 11\n",
      drive.c_cylinder
      );
    r++;
  }

  if( drive.tr00 ) {
    printf(
      "wd_fdc_standard_step_in_test: TR00 is set, expected clear\n"
      );
    r++;
  }

  if( fdc->direction != FDD_STEP_IN ) {
    printf(
      "wd_fdc_standard_step_in_test: direction is %d, expected STEP IN\n",
      fdc->direction
      );
    r++;
  }

  if( fdc->track_register != 0 ) {
    printf(
      "wd_fdc_standard_step_in_test: track register is %u, expected 0\n",
      (unsigned)fdc->track_register
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_standard_step_in_test failed\n" );

  return r;
}


/*
 * Verify the original Fuse STEP OUT behavior.
 */
static int
wd_fdc_standard_step_out_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc( &drive );

  wd_fdc_cr_write( fdc, 0x60 );

  if( drive.c_cylinder != 9 ) {
    printf(
      "wd_fdc_standard_step_out_test: cylinder is %d, expected 9\n",
      drive.c_cylinder
      );
    r++;
  }

  if( drive.tr00 ) {
    printf(
      "wd_fdc_standard_step_out_test: TR00 is set, expected clear\n"
      );
    r++;
  }

  if( fdc->direction != FDD_STEP_OUT ) {
    printf(
      "wd_fdc_standard_step_out_test: direction is %d, expected STEP OUT\n",
      fdc->direction
      );
    r++;
  }

  if( fdc->track_register != 0 ) {
    printf(
      "wd_fdc_standard_step_out_test: track register is %u, expected 0\n",
      (unsigned)fdc->track_register
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_standard_step_out_test failed\n" );

  return r;
}


/*
 * Verify STEP IN through the optional callback.
 */
static int
wd_fdc_callback_step_in_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  step_callback_count = 0;
  step_callback_direction = -1;

  fdc = init_test_fdc( &drive );

  wd_fdc_set_step_callback( fdc, test_step_callback );

  wd_fdc_cr_write( fdc, 0x40 );

  if( step_callback_count != 1 ) {
    printf(
      "wd_fdc_callback_step_in_test: callback count is %d, expected 1\n",
      step_callback_count
      );
    r++;
  }

  if( step_callback_direction != FDD_STEP_IN ) {
    printf(
      "wd_fdc_callback_step_in_test: direction is %d, expected STEP IN\n",
      step_callback_direction
      );
    r++;
  }

  if( drive.c_cylinder != 10 ) {
    printf(
      "wd_fdc_callback_step_in_test: current drive moved to %d, expected 10\n",
      drive.c_cylinder
      );
    r++;
  }

  if( fdc->track_register != 0 ) {
    printf(
      "wd_fdc_callback_step_in_test: track register is %u, expected 0\n",
      (unsigned)fdc->track_register
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_callback_step_in_test failed\n" );

  return r;
}


/*
 * Verify STEP OUT through the optional callback.
 */
static int
wd_fdc_callback_step_out_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  step_callback_count = 0;
  step_callback_direction = -1;

  fdc = init_test_fdc( &drive );

  wd_fdc_set_step_callback( fdc, test_step_callback );

  wd_fdc_cr_write( fdc, 0x60 );

  if( step_callback_count != 1 ) {
    printf(
      "wd_fdc_callback_step_out_test: callback count is %d, expected 1\n",
      step_callback_count
      );
    r++;
  }

  if( step_callback_direction != FDD_STEP_OUT ) {
    printf(
      "wd_fdc_callback_step_out_test: direction is %d, expected STEP OUT\n",
      step_callback_direction
      );
    r++;
  }

  if( drive.c_cylinder != 10 ) {
    printf(
      "wd_fdc_callback_step_out_test: current drive moved to %d, expected 10\n",
      drive.c_cylinder
      );
    r++;
  }

  if( fdc->track_register != 0 ) {
    printf(
      "wd_fdc_callback_step_out_test: track register is %u, expected 0\n",
      (unsigned)fdc->track_register
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_callback_step_out_test failed\n" );

  return r;
}


/*
 * Verify the historical multisector behavior.
 *
 * Without WD_FLAG_DELAY_SECTOR_INC the Sector Register is incremented
 * immediately when the current sector completes.
 */
static int
wd_fdc_standard_sector_increment_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags( &drive, WD_FLAG_DRQ );

  prepare_multisector_write( fdc, 5 );

  wd_fdc_dr_write( fdc, 0x55 );

  if( fdc->sector_register != 6 ) {
    printf(
      "wd_fdc_standard_sector_increment_test: sector is %u, expected 6\n",
      (unsigned)fdc->sector_register
      );
    r++;
  }

  if( fdc->sector_increment_pending ) {
    printf(
      "wd_fdc_standard_sector_increment_test: increment is pending, "
      "expected clear\n"
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_standard_sector_increment_test failed\n" );

  return r;
}


/*
 * Verify delayed multisector Sector Register semantics.
 */
static int
wd_fdc_delayed_sector_increment_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags(
    &drive,
    WD_FLAG_DRQ | WD_FLAG_DELAY_SECTOR_INC
    );

  prepare_multisector_write( fdc, 5 );

  wd_fdc_dr_write( fdc, 0x55 );

  if( fdc->sector_register != 5 ) {
    printf(
      "wd_fdc_delayed_sector_increment_test: sector before event is %u, "
      "expected 5\n",
      (unsigned)fdc->sector_register
      );
    r++;
  }

  if( !fdc->sector_increment_pending ) {
    printf(
      "wd_fdc_delayed_sector_increment_test: increment is not pending\n"
      );
    r++;
  }

  /*
   * sector_increment_pending is processed by wd_fdc_event() before the
   * state-specific dispatch. Setting NONE here prevents the artificial
   * drive from entering a physical next-sector search.
   */
  fdc->state = WD_FDC_STATE_NONE;

  if( wdfdc_test_run_next_event() ) {

    printf(
      "wd_fdc_delayed_sector_increment_test: "
      "no FDC event was scheduled\n"
      );
    r++;

  } else {

    if( fdc->sector_register != 6 ) {
      printf(
        "wd_fdc_delayed_sector_increment_test: sector after event is %u, "
        "expected 6\n",
        (unsigned)fdc->sector_register
        );
      r++;
    }

    if( fdc->sector_increment_pending ) {
      printf(
        "wd_fdc_delayed_sector_increment_test: "
        "increment remains pending after event\n"
        );
      r++;
    }
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_delayed_sector_increment_test failed\n" );

  return r;
}


/*
 * Verify delayed Sector Register semantics and event ownership after a
 * completed multisector READ record.
 */
static int
wd_fdc_delayed_read_sector_increment_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  libspectrum_dword expected_time;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags(
    &drive,
    WD_FLAG_DRQ | WD_FLAG_DELAY_SECTOR_INC
    );

  prepare_multisector_read( fdc, &drive, 5 );
  expected_time = tstates + 2 * machine_current->timings.processor_speed / 100;

  wd_fdc_dr_read( fdc );

  if( fdc->sector_register != 5 ) {
    printf(
      "wd_fdc_delayed_read_sector_increment_test: "
      "sector before event is %u, expected 5\n",
      (unsigned)fdc->sector_register
      );
    r++;
  }

  if( !fdc->sector_increment_pending ) {
    printf(
      "wd_fdc_delayed_read_sector_increment_test: "
      "increment is not pending\n"
      );
    r++;
  }

  if( wdfdc_test_event_count( "WD FDC event" ) != 1 ) {
    printf(
      "wd_fdc_delayed_read_sector_increment_test: "
      "expected exactly one WD FDC event, found %d\n",
      wdfdc_test_event_count( "WD FDC event" )
      );
    r++;
  }

  if( wdfdc_test_event_time( "WD FDC event" ) != expected_time ) {
    printf(
      "wd_fdc_delayed_read_sector_increment_test: "
      "inter-sector event time is %u, expected %u\n",
      (unsigned)wdfdc_test_event_time( "WD FDC event" ),
      (unsigned)expected_time
      );
    r++;
  }

  /* Isolate the delayed increment from the artificial next-sector search. */
  fdc->state = WD_FDC_STATE_NONE;

  if( wdfdc_test_run_next_event() ) {
    printf(
      "wd_fdc_delayed_read_sector_increment_test: "
      "no inter-sector event was scheduled\n"
      );
    r++;
  } else {
    if( fdc->sector_register != 6 ) {
      printf(
        "wd_fdc_delayed_read_sector_increment_test: "
        "sector after event is %u, expected 6\n",
        (unsigned)fdc->sector_register
        );
      r++;
    }

    if( fdc->sector_increment_pending ) {
      printf(
        "wd_fdc_delayed_read_sector_increment_test: "
        "increment remains pending after event\n"
        );
      r++;
    }
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_delayed_read_sector_increment_test failed\n" );

  return r;
}


/*
 * Verify the historical Fuse Force Interrupt behavior.
 *
 * Without the preservation flag, active Type II status is converted to
 * Type I status. BUSY, WRPROT, CRCERR and DRQ are cleared, while RNF
 * retains the historical value.
 */
static int
wd_fdc_standard_force_interrupt_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags( &drive, WD_FLAG_NONE );

  prepare_force_interrupt( fdc );

  /*
   * 0xd0 = Force Interrupt without requesting immediate INTRQ.
   */
  wd_fdc_cr_write( fdc, 0xd0 );

  if( fdc->state != WD_FDC_STATE_NONE ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: state is %d, expected NONE\n",
      fdc->state
      );
    r++;
  }

  if( fdc->status_type != WD_FDC_STATUS_TYPE1 ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: status type is %d, "
      "expected TYPE1\n",
      fdc->status_type
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_BUSY ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: BUSY remains set\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_WRPROT ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: WRPROT remains set\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_CRCERR ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: CRCERR remains set\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_IDX_DRQ ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: DRQ remains set\n"
      );
    r++;
  }

  if( fdc->datarq ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: datarq remains asserted\n"
      );
    r++;
  }

  if( datarq_reset_count != 1 ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: DRQ reset count is %d, "
      "expected 1\n",
      datarq_reset_count
      );
    r++;
  }

  if( intrq_set_count != 0 ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: INTRQ set count is %d, "
      "expected 0 for command D0\n",
      intrq_set_count
      );
    r++;
  }

  /*
   * Cylinder 10 means Type I TRACK00 must be clear.
   */
  if( fdc->status_register & WD_FDC_SR_LOST ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: TRACK00/LOST is set, "
      "expected clear\n"
      );
    r++;
  }

  /*
   * Historical Fuse Force Interrupt does not clear RNF.
   */
  if( !( fdc->status_register & WD_FDC_SR_RNF ) ) {
    printf(
      "wd_fdc_standard_force_interrupt_test: RNF was cleared\n"
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_standard_force_interrupt_test failed\n" );

  return r;
}


/*
 * Verify preserved Type II/III status during Force Interrupt.
 *
 * BUSY and DRQ are cleared, but Type II status and error bits remain
 * available for inspection.
 */
static int
wd_fdc_preserve_status_force_interrupt_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags(
    &drive,
    WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT
    );

  prepare_force_interrupt( fdc );

  wd_fdc_cr_write( fdc, 0xd0 );

  if( fdc->state != WD_FDC_STATE_NONE ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: state is %d, "
      "expected NONE\n",
      fdc->state
      );
    r++;
  }

  if( fdc->status_type != WD_FDC_STATUS_TYPE2 ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: status type is %d, "
      "expected TYPE2\n",
      fdc->status_type
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_BUSY ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: BUSY remains set\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_IDX_DRQ ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: DRQ remains set\n"
      );
    r++;
  }

  if( fdc->datarq ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: datarq remains asserted\n"
      );
    r++;
  }

  if( datarq_reset_count != 1 ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: DRQ reset count is %d, "
      "expected 1\n",
      datarq_reset_count
      );
    r++;
  }

  if( intrq_set_count != 0 ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: INTRQ set count is %d, "
      "expected 0 for command D0\n",
      intrq_set_count
      );
    r++;
  }

  if( !( fdc->status_register & WD_FDC_SR_WRPROT ) ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: WRPROT was cleared\n"
      );
    r++;
  }

  if( !( fdc->status_register & WD_FDC_SR_RNF ) ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: RNF was cleared\n"
      );
    r++;
  }

  if( !( fdc->status_register & WD_FDC_SR_CRCERR ) ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: CRCERR was cleared\n"
      );
    r++;
  }

  if( !( fdc->status_register & WD_FDC_SR_LOST ) ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: LOST was cleared\n"
      );
    r++;
  }

  if( fdc->sector_increment_pending ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: "
      "sector increment remains pending\n"
      );
    r++;
  }

  if( fdc->read_id ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: read_id remains set\n"
      );
    r++;
  }

  if( fdc->id_mark != WD_FDC_AM_NONE ) {
    printf(
      "wd_fdc_preserve_status_force_interrupt_test: id_mark is %d, "
      "expected NONE\n",
      fdc->id_mark
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_preserve_status_force_interrupt_test failed\n" );

  return r;
}


/*
 * Verify the immediate-interrupt bit of the Type IV command.
 *
 * Command 0xd8 is Force Interrupt with the immediate-interrupt condition
 * enabled. In addition to terminating the active command, the WD FDC must
 * assert INTRQ.
 */
static int
wd_fdc_force_interrupt_intrq_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags(
    &drive,
    WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT
    );

  prepare_force_interrupt( fdc );

  /*
   * D8 = Force Interrupt + immediate interrupt.
   */
  wd_fdc_cr_write( fdc, 0xd8 );

  if( intrq_set_count != 1 ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: INTRQ set count is %d, "
      "expected 1\n",
      intrq_set_count
      );
    r++;
  }

  if( fdc->intrq != 1 ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: intrq is %d, expected 1\n",
      fdc->intrq
      );
    r++;
  }

  if( datarq_reset_count != 1 ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: DRQ reset count is %d, "
      "expected 1\n",
      datarq_reset_count
      );
    r++;
  }

  if( fdc->datarq ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: DRQ remains asserted\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_BUSY ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: BUSY remains set\n"
      );
    r++;
  }

  if( fdc->state != WD_FDC_STATE_NONE ) {
    printf(
      "wd_fdc_force_interrupt_intrq_test: state is %d, expected NONE\n",
      fdc->state
      );
    r++;
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_force_interrupt_intrq_test failed\n" );

  return r;
}


/*
 * Verify that a WD1770 Force Interrupt with no interrupt condition
 * still schedules the normal spindle motor timeout.
 *
 * D0 terminates the current command without asserting INTRQ. The
 * WD1770 motor must remain on immediately after the command, then
 * turn off after the normal idle timeout.
 */
static int
wd_fdc_force_interrupt_motor_timeout_test( void )
{
  wd_fdc *fdc;
  fdd_t drive;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );

  fdc = init_test_fdc_flags( &drive, WD_FLAG_NONE );

  prepare_force_interrupt( fdc );

  /*
   * D0 = Force Interrupt without requesting immediate INTRQ.
   */
  wd_fdc_cr_write( fdc, 0xd0 );

  /*
   * Force Interrupt terminates the command immediately, but the motor
   * remains running until the normal WD1770 idle timeout expires.
   */
  if( !( fdc->status_register & WD_FDC_SR_MOTORON ) ) {
    printf(
      "wd_fdc_force_interrupt_motor_timeout_test: "
      "motor stopped immediately after D0\n"
      );
    r++;
  }

  if( fdc->status_register & WD_FDC_SR_BUSY ) {
    printf(
      "wd_fdc_force_interrupt_motor_timeout_test: "
      "BUSY remains set after D0\n"
      );
    r++;
  }

  if( intrq_set_count != 0 ) {
    printf(
      "wd_fdc_force_interrupt_motor_timeout_test: "
      "INTRQ set count is %d, expected 0 for command D0\n",
      intrq_set_count
      );
    r++;
  }

  /*
   * There should now be a motor-off event scheduled for the normal
   * WD1770 idle timeout.
   */
  if( wdfdc_test_run_next_event() ) {

    printf(
      "wd_fdc_force_interrupt_motor_timeout_test: "
      "no motor-off event was scheduled\n"
      );
    r++;

  } else {

    if( fdc->status_register & WD_FDC_SR_MOTORON ) {
      printf(
        "wd_fdc_force_interrupt_motor_timeout_test: "
        "motor remains on after timeout event\n"
        );
      r++;
    }

    /*
     * D0 itself must not assert INTRQ, nor should the motor timeout.
     */
    if( intrq_set_count != 0 ) {
      printf(
        "wd_fdc_force_interrupt_motor_timeout_test: "
        "INTRQ set count after timeout is %d, expected 0\n",
        intrq_set_count
        );
      r++;
    }
  }

  free_test_fdc( fdc );

  if( r )
    printf( "wd_fdc_force_interrupt_motor_timeout_test failed\n" );

  return r;
}


/* Reference: WD1770/1772 datasheet, Type II/IV Commands and Status Register:
   https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/WesternDigital/WD1772.pdf

   A synthetic MFM track with two 128-byte sectors. CRC constants use the
   WD polynomial x^16+x^12+x^5+1, initial value 0xffff, including A1 marks. */
static void
prepare_transfer_track( fdd_t *drive, libspectrum_byte *track,
                         libspectrum_byte *clocks, libspectrum_byte *fm,
                         libspectrum_byte *weak )
{
  static const libspectrum_byte id_crc[][2] = { { 0xea, 0x2d }, { 0xbf, 0x7e } };
  static const libspectrum_byte data_crc[][2] = { { 0x65, 0xf5 }, { 0x53, 0x28 } };
  int sector, base, i;

  memset( track, 0x4e, 6250 );
  memset( clocks, 0, DISK_CLEN( 6250 ) );
  memset( fm, 0, DISK_CLEN( 6250 ) );
  memset( weak, 0, DISK_CLEN( 6250 ) );
  for( sector = 0; sector < 2; sector++ ) {
    base = sector * 256;
    for( i = 16; i < 19; i++ ) {
      track[base + i] = 0xa1;
      bitmap_set( clocks, base + i );
    }
    track[base + 19] = 0xfe;
    track[base + 20] = track[base + 21] = track[base + 23] = 0;
    track[base + 22] = sector + 1;
    memcpy( track + base + 24, id_crc[sector], 2 );
    memset( track + base + 48, 0, 12 );
    for( i = 60; i < 63; i++ ) {
      track[base + i] = 0xa1;
      bitmap_set( clocks, base + i );
    }
    track[base + 63] = 0xfb;
    memset( track + base + 64, ( sector + 1 ) * 0x11, 128 );
    memcpy( track + base + 192, data_crc[sector], 2 );
  }

  init_test_drive( drive, 0 );
  drive->selected = drive->loaded = drive->ready = drive->loadhead = 1;
  drive->disk.track = track;
  drive->disk.clocks = clocks;
  drive->disk.fm = fm;
  drive->disk.weak = weak;
  drive->disk.c_bpt = 6250;
  fdd_motoron( drive, 1 );
}

/* Advance byte timing through real FDD/WD callbacks in the bounded test
   scheduler. Do not manufacture the next sector's state or register values. */
static int
transfer_wait_drq( wd_fdc *fdc, int advance )
{
  int events;

  for( events = 0; events < 64; events++ ) {
    if( !( fdc->status_register & WD_FDC_SR_BUSY ) ) return 1;
    if( !advance && fdc->datarq ) return 0;
    if( wdfdc_test_run_next_event() ) return 1;
    advance = 0;
  }
  return 1;
}

static int
wd_fdc_multisector_transfer_test( int write, int delayed )
{
  libspectrum_byte track[6250], clocks[DISK_CLEN( 6250 )];
  libspectrum_byte fm[DISK_CLEN( 6250 )], weak[DISK_CLEN( 6250 )];
  fdd_t drive;
  wd_fdc *fdc;
  unsigned int flags = WD_FLAG_DRQ;
  int sector, byte, r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  prepare_transfer_track( &drive, track, clocks, fm, weak );
  if( delayed )
    flags |= WD_FLAG_DELAY_SECTOR_INC | WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT;
  fdc = init_test_fdc_flags( &drive, flags );
  fdc->dden = 1;
  wd_fdc_sec_write( fdc, 1 );
  wd_fdc_cr_write( fdc, write ? 0xb8 : 0x98 );

  for( sector = 1; sector <= 2; sector++ ) {
    for( byte = 0; byte < 128; byte++ ) {
      if( transfer_wait_drq( fdc, sector != 1 || byte != 0 ) ||
          fdc->sector_register != sector || fdc->sector_increment_pending ) {
        printf( "multisector %s (delayed=%d): sector %d byte %d not ready\n",
                write ? "write" : "read", delayed, sector, byte );
        r++; goto cleanup;
      }
      if( write ) {
        wd_fdc_dr_write( fdc, ( sector + 2 ) * 0x11 );
      } else if( wd_fdc_dr_read( fdc ) != sector * 0x11 ) {
        printf( "multisector read (delayed=%d): sector %d byte %d mismatch\n",
                delayed, sector, byte );
        r++; goto cleanup;
      }
    }
    if( !( fdc->status_register & WD_FDC_SR_BUSY ) || fdc->datarq ||
        fdc->sector_register != sector + !delayed ||
        fdc->sector_increment_pending != delayed ||
        ( delayed && wdfdc_test_event_count( "WD FDC event" ) != 1 ) ) {
      printf( "multisector %s (delayed=%d): invalid sector %d continuation\n",
              write ? "write" : "read", delayed, sector );
      r++; goto cleanup;
    }
  }
  if( write ) {
    for( sector = 0; sector < 2; sector++ ) {
      for( byte = 0; byte < 128; byte++ ) {
        if( track[sector * 256 + 64 + byte] != ( sector + 3 ) * 0x11 ) {
          printf( "multisector write: sector %d byte %d was not stored\n",
                  sector + 1, byte );
          r++; goto cleanup;
        }
      }
    }
  }

  /* SAFE ends a multisector command using D0 before asking for sector 3. */
  wd_fdc_cr_write( fdc, 0xd0 );
  if( ( fdc->status_register & WD_FDC_SR_BUSY ) || fdc->datarq || fdc->intrq ||
      wdfdc_test_event_count( "WD FDC event" ) ||
      ( delayed && ( fdc->sector_increment_pending ||
                     wdfdc_test_event_count( "WD FDC timeout" ) ) ) ) {
    printf( "multisector %s (delayed=%d): D0 left transfer activity pending\n",
            write ? "write" : "read", delayed );
    r++;
  }
  if( write ) {
    /* Read both records back through the controller, checking the written
       address marks and CRCs as well as the payload and next-sector path. */
    wd_fdc_sec_write( fdc, 1 );
    wd_fdc_cr_write( fdc, 0x98 );
    for( sector = 1; sector <= 2; sector++ ) {
      for( byte = 0; byte < 128; byte++ ) {
        if( transfer_wait_drq( fdc, sector != 1 || byte != 0 ) ||
            wd_fdc_dr_read( fdc ) != ( sector + 2 ) * 0x11 ) {
          printf( "multisector write/readback (delayed=%d): "
                  "sector %d byte %d failed\n", delayed, sector, byte );
          r++; goto cleanup;
        }
      }
    }
    if( !( fdc->status_register & WD_FDC_SR_BUSY ) ||
        ( fdc->status_register & WD_FDC_SR_CRCERR ) ) r++;
    wd_fdc_cr_write( fdc, 0xd0 );
  }

cleanup:
  fdd_motoron( &drive, 0 );
  free_test_fdc( fdc );
  return r;
}

/* WD1770 Status Register: Force Interrupt while idle returns Type I
   status even when active-command status preservation is enabled. */
static int
wd_fdc_idle_force_interrupt_test( void )
{
  fdd_t drive;
  wd_fdc *fdc;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 0 );
  fdc = init_test_fdc_flags( &drive, WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT );
  prepare_force_interrupt( fdc );
  fdc->status_register &= ~WD_FDC_SR_BUSY;
  fdc->state = WD_FDC_STATE_NONE;
  fdc->read_id = fdc->sector_increment_pending = 0;
  fdc->id_mark = WD_FDC_AM_NONE;
  wd_fdc_cr_write( fdc, 0xd0 );
  if( fdc->status_type != WD_FDC_STATUS_TYPE1 || fdc->datarq || fdc->intrq ||
      ( fdc->status_register & ( WD_FDC_SR_BUSY | WD_FDC_SR_CRCERR |
                                 WD_FDC_SR_WRPROT | WD_FDC_SR_IDX_DRQ ) ) ||
      !( fdc->status_register & WD_FDC_SR_LOST ) ) {
    printf( "wd_fdc_idle_force_interrupt_test: idle status was not Type I\n" );
    r++;
  }
  free_test_fdc( fdc );
  return r;
}

static int
wd_fdc_d0_motor_lifecycle_test( void )
{
  fdd_t drive;
  wd_fdc *fdc;
  int r = 0;

  wdfdc_test_reset_events();
  reset_signal_counters();
  init_test_drive( &drive, 10 );
  fdc = init_test_fdc_flags( &drive, WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT );
  prepare_force_interrupt( fdc );
  wd_fdc_cr_write( fdc, 0xd0 );
  tstates += machine_current->timings.processor_speed / 2;
  wd_fdc_cr_write( fdc, 0xd0 );
  if( wdfdc_test_event_count( "WD FDC motor off" ) != 1 ||
      wdfdc_test_event_time( "WD FDC motor off" ) !=
        tstates + 2 * machine_current->timings.processor_speed || fdc->intrq ) {
    printf( "wd_fdc_d0_motor_lifecycle_test: repeated D0 left a stale timer\n" );
    r++;
  }
  /* A subsequent accepted command must cancel the idle motor timer. */
  wd_fdc_dr_write( fdc, 1 );
  wd_fdc_cr_write( fdc, 0x18 ); /* SEEK, spin-up disabled */
  if( wdfdc_test_event_count( "WD FDC motor off" ) ||
      !( fdc->status_register & WD_FDC_SR_BUSY ) ||
      !( fdc->status_register & WD_FDC_SR_MOTORON ) ) {
    printf( "wd_fdc_d0_motor_lifecycle_test: new command did not cancel timer\n" );
    r++;
  }
  free_test_fdc( fdc );
  return r;
}

int
main( void )
{
  int r = 0;

  wdfdc_test_machine_init();

  /*
   * The fake startup manager immediately executes the FDD startup callback.
   * This registers the real FDD and WD FDC event handlers with the fake
   * event scheduler.
   */
  fdd_register_startup();

  /*
   * Type I STEP compatibility and callback tests.
   */
  r += wd_fdc_standard_step_in_test();
  r += wd_fdc_standard_step_out_test();
  r += wd_fdc_callback_step_in_test();
  r += wd_fdc_callback_step_out_test();

  /*
   * Type II multisector Sector Register compatibility tests.
   */
  r += wd_fdc_standard_sector_increment_test();
  r += wd_fdc_delayed_sector_increment_test();
  r += wd_fdc_delayed_read_sector_increment_test();
  r += wd_fdc_multisector_transfer_test( 0, 0 );
  r += wd_fdc_multisector_transfer_test( 0, 1 );
  r += wd_fdc_multisector_transfer_test( 1, 0 );
  r += wd_fdc_multisector_transfer_test( 1, 1 );

  /*
   * Type IV Force Interrupt compatibility and signal tests.
   */
  r += wd_fdc_standard_force_interrupt_test();
  r += wd_fdc_preserve_status_force_interrupt_test();
  r += wd_fdc_force_interrupt_intrq_test();
  r += wd_fdc_force_interrupt_motor_timeout_test();
  r += wd_fdc_d0_motor_lifecycle_test();
  r += wd_fdc_idle_force_interrupt_test();

  if( r ) {
    printf(
      "\nWD FDC standalone tests: %d failure%s\n",
      r,
      r == 1 ? "" : "s"
      );

    return 1;
  }

  printf( "WD FDC standalone tests: OK\n" );

  return 0;
}
