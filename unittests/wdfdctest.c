/* wdfdctest.c: standalone tests for the WD FDC emulation */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "libspectrum.h"

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

  /*
   * Type IV Force Interrupt compatibility and signal tests.
   */
  r += wd_fdc_standard_force_interrupt_test();
  r += wd_fdc_preserve_status_force_interrupt_test();
  r += wd_fdc_force_interrupt_intrq_test();
  r += wd_fdc_force_interrupt_motor_timeout_test();

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
