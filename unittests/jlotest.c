/* jlotest.c: standalone tests for JLO Disk System drive logic */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "libspectrum.h"

#include "peripherals/disk/jlo.h"
#include "peripherals/disk/jlo_logic.h"
#include "peripherals/disk/fdd.h"
#include "peripherals/disk/wd_fdc.h"

/*
 * Functions supplied by wdfdc_fakes.c.
 */
void wdfdc_test_machine_init( void );
void wdfdc_test_reset_events( void );
int wdfdc_test_run_next_event( void );
int wdfdc_test_run_event( const char *description );


typedef struct jlo_test_context {

  wd_fdc *fdc;
  fdd_t drives[ JLO_NUM_DRIVES ];
  fdd_t unselected_drive;
  libspectrum_byte dsel;

} jlo_test_context;


/*
 * wd_fdc step callbacks do not carry a context pointer. The real JLO
 * implementation has static JLO state for the same reason.
 */
static jlo_test_context *step_context;

static void jlo_test_step( int direction );
static void jlo_test_motor( int on );


/*
 * Initialise one artificial JLO drive.
 *
 * No disk image is needed for STEP and selection tests.
 */
static void
init_test_drive( fdd_t *drive, int cylinder )
{
  memset( drive, 0, sizeof( *drive ) );

  drive->type = FDD_SHUGART;
  drive->fdd_heads = 2;
  drive->fdd_cylinders = 80;

  drive->c_cylinder = cylinder;
  drive->c_head = 0;

  drive->tr00 = cylinder == 0;
}


/*
 * Initialise the four-drive JLO test environment.
 */
static void
jlo_test_init( jlo_test_context *jlo )
{
  int i;

  memset( jlo, 0, sizeof( *jlo ) );

  for( i = 0; i < JLO_NUM_DRIVES; i++ )
    init_test_drive( &jlo->drives[ i ], 10 + i * 10 );

  fdd_init( &jlo->unselected_drive, FDD_TYPE_NONE, NULL, 0 );

  jlo->fdc = wd_fdc_alloc_fdc(
    WD1770,
    0,
    WD_FLAG_DRQ |
    WD_FLAG_DELAY_SECTOR_INC |
    WD_FLAG_PRESERVE_STATUS_ON_FORCE_INTERRUPT
    );

  /*
   * A normal peripheral installs or explicitly clears these callbacks.
   * The standalone test has no external interrupt wiring.
   */
  jlo->fdc->set_intrq = NULL;
  jlo->fdc->reset_intrq = NULL;
  jlo->fdc->set_datarq = NULL;
  jlo->fdc->reset_datarq = NULL;

  jlo->fdc->current_drive = &jlo->drives[ JLO_DRIVE_0 ];

  fdd_select( &jlo->drives[ JLO_DRIVE_0 ], 1 );

  jlo->dsel = 0x01;

  /*
   * JLO uses the same MFM density setting as the WD1770 implementation
   * used by the interface.
   */
  jlo->fdc->dden = 1;

  step_context = jlo;

  wd_fdc_set_step_callback( jlo->fdc, jlo_test_step );
  wd_fdc_set_motor_callback( jlo->fdc, jlo_test_motor );
}


/*
 * Clean up a JLO test context.
 */
static void
jlo_test_end( jlo_test_context *jlo )
{
  int i;

  /*
   * If the motor test left a drive running, restore the FDD global motor
   * count before freeing the context.
   */
  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {

    if( jlo->drives[ i ].motoron )
      fdd_motoron( &jlo->drives[ i ], 0 );
  }

  wdfdc_test_reset_events();

  libspectrum_free( jlo->fdc );
  jlo->fdc = NULL;

  if( step_context == jlo )
    step_context = NULL;
}


/*
 * JLO Board B STEP/DIRECTION wiring.
 *
 * Board B can assert more than one drive-select line simultaneously.
 * STEP and DIRECTION are common signals, therefore every selected drive
 * sees the same physical step pulse.
 *
 * This is the JLO-specific callback installed in wd_fdc.
 */
static void
jlo_test_step( int direction )
{
  jlo_logic_step( step_context->drives, step_context->dsel, direction );
}


static void
jlo_test_motor( int on )
{
  jlo_logic_motor( step_context->drives, step_context->dsel, on );
}


/*
 * JLO port B7 behavior.
 *
 * bits 0..3 = drive-select mask
 * bit 7     = side select
 *
 * The current WD FDC drive is retained while it remains selected.
 * Otherwise, the first selected drive becomes current.
 *
 * MOTOR ON is a WD1770 controller state. When B7 changes while the motor
 * is already active, that state must be propagated to the newly selected
 * drive(s); otherwise the next sector search can wait forever for index.
 */
static void
jlo_test_dsel_write( jlo_test_context *jlo, libspectrum_byte b )
{
  jlo_logic_select( jlo->fdc, jlo->drives, &jlo->unselected_drive,
                    &jlo->dsel, b );
}


/*
 * JLO-specific Data Register read behavior.
 *
 * SAFE can perform dummy reads while a multisector command is BUSY but
 * DRQ is inactive. Such a read must return the cached WD data register
 * without consuming another byte.
 */
static libspectrum_byte
jlo_test_dr_read( jlo_test_context *jlo )
{
  return jlo_logic_data_read( jlo->fdc );
}


/*
 * Verify the B7 drive-select mask and side-select bit.
 *
 * Drives B and D are selected simultaneously. Side 1 must be presented
 * to all four drives because the side-select signal is common.
 */
static int
jlo_drive_select_test( void )
{
  jlo_test_context jlo;
  int i;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  /*
   * 0x8a:
   *
   *   bit 7 = side 1
   *   bit 3 = drive D
   *   bit 1 = drive B
   */
  jlo_test_dsel_write( &jlo, 0x8a );

  if( jlo.drives[ JLO_DRIVE_0 ].selected ) {
    printf( "jlo_drive_select_test: drive A selected unexpectedly\n" );
    r++;
  }

  if( !jlo.drives[ JLO_DRIVE_1 ].selected ) {
    printf( "jlo_drive_select_test: drive B not selected\n" );
    r++;
  }

  if( jlo.drives[ JLO_DRIVE_2 ].selected ) {
    printf( "jlo_drive_select_test: drive C selected unexpectedly\n" );
    r++;
  }

  if( !jlo.drives[ JLO_DRIVE_3 ].selected ) {
    printf( "jlo_drive_select_test: drive D not selected\n" );
    r++;
  }

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {

    if( jlo.drives[ i ].c_head != 1 ) {
      printf(
        "jlo_drive_select_test: drive %c head is %d, expected 1\n",
        'A' + i,
        jlo.drives[ i ].c_head
        );
      r++;
    }
  }

  /*
   * A was current but is no longer selected. B is the first selected
   * drive and must therefore become current.
   */
  if( jlo.fdc->current_drive != &jlo.drives[ JLO_DRIVE_1 ] ) {
    printf(
      "jlo_drive_select_test: current drive is not B\n"
      );
    r++;
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_drive_select_test failed\n" );

  return r;
}


/*
 * Verify that current_drive is not unnecessarily changed.
 *
 * If drive D is already current and B+D remain selected, D must remain
 * current even though B is the first set bit in the B7 mask.
 */
static int
jlo_current_drive_retention_test( void )
{
  jlo_test_context jlo;
  int events = 0;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  jlo.fdc->current_drive =
    &jlo.drives[ JLO_DRIVE_3 ];

  jlo_test_dsel_write( &jlo, 0x0a );

  if( jlo.fdc->current_drive !=
      &jlo.drives[ JLO_DRIVE_3 ] ) {

    printf(
      "jlo_current_drive_retention_test: "
      "current drive changed, expected D\n"
      );
    r++;
  }

  /*
   * Remove D from the selection. B is still selected and must now become
   * the FDC current drive.
   */
  jlo_test_dsel_write( &jlo, 0x02 );

  if( jlo.fdc->current_drive !=
      &jlo.drives[ JLO_DRIVE_1 ] ) {

    printf(
      "jlo_current_drive_retention_test: "
      "current drive is not B after deselecting D\n"
      );
    r++;
  }

  /* No selected drives must use the safe unloaded dummy FDD. */
  jlo_test_dsel_write( &jlo, 0x00 );

  if( jlo.fdc->current_drive != &jlo.unselected_drive ) {

    printf(
      "jlo_current_drive_retention_test: "
      "current drive is not the unselected drive with B7=00\n"
      );
    r++;
  }

  /*
   * Exercise the real WD command path with no physical drive selected.
   * The command must terminate safely instead of dereferencing NULL.
   */
  wd_fdc_cr_write( jlo.fdc, 0x80 );

  while( ( jlo.fdc->status_register & WD_FDC_SR_BUSY ) && events++ < 16 ) {
    if( wdfdc_test_run_next_event() )
      break;
  }

  if( jlo.fdc->status_register & WD_FDC_SR_BUSY ) {
    printf(
      "jlo_current_drive_retention_test: "
      "read command remains busy with B7=00\n"
      );
    r++;
  }

  if( jlo.fdc->current_drive != &jlo.unselected_drive ) {
    printf(
      "jlo_current_drive_retention_test: "
      "read command changed the unselected drive\n"
      );
    r++;
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_current_drive_retention_test failed\n" );

  return r;
}


/*
 * Verify JLO parallel STEP behavior through the real wd_fdc callback path.
 *
 * Drives B and D are selected. A WD1770 STEP IN command must move both B
 * and D while A and C remain at their original cylinders.
 */
static int
jlo_multiple_drive_step_test( void )
{
  jlo_test_context jlo;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  /*
   * Initial cylinders:
   *
   * A = 10
   * B = 20
   * C = 30
   * D = 40
   */
  jlo_test_dsel_write( &jlo, 0x0a );

  wd_fdc_set_step_callback(
    jlo.fdc,
    jlo_test_step
    );

  /*
   * Skip motor spin-up; this test is about the physical STEP pulse.
   */
  jlo.fdc->status_register |= WD_FDC_SR_MOTORON;

  /*
   * 0x40 = STEP IN without Track Register update.
   */
  wd_fdc_cr_write( jlo.fdc, 0x40 );

  if( jlo.drives[ JLO_DRIVE_0 ].c_cylinder != 10 ) {
    printf(
      "jlo_multiple_drive_step_test: A moved, expected 10\n"
      );
    r++;
  }

  if( jlo.drives[ JLO_DRIVE_1 ].c_cylinder != 21 ) {
    printf(
      "jlo_multiple_drive_step_test: B is %d, expected 21\n",
      jlo.drives[ JLO_DRIVE_1 ].c_cylinder
      );
    r++;
  }

  if( jlo.drives[ JLO_DRIVE_2 ].c_cylinder != 30 ) {
    printf(
      "jlo_multiple_drive_step_test: C moved, expected 30\n"
      );
    r++;
  }

  if( jlo.drives[ JLO_DRIVE_3 ].c_cylinder != 41 ) {
    printf(
      "jlo_multiple_drive_step_test: D is %d, expected 41\n",
      jlo.drives[ JLO_DRIVE_3 ].c_cylinder
      );
    r++;
  }

  /*
   * Command 0x40 does not update the WD Track Register.
   */
  if( jlo.fdc->track_register != 0 ) {
    printf(
      "jlo_multiple_drive_step_test: Track Register is %u, expected 0\n",
      (unsigned)jlo.fdc->track_register
      );
    r++;
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_multiple_drive_step_test failed\n" );

  return r;
}


/*
 * Verify the motor-state synchronization required when B7 changes while
 * the WD1770 MOTOR ON state is already active.
 *
 * This specifically covers the case which caused SAFE MOVE to wait
 * indefinitely for an index pulse after changing drives.
 */
static int
jlo_drive_change_motor_test( void )
{
  jlo_test_context jlo;
  int i;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  /*
   * fdd_motoron() only models a motor for a loaded drive. A real disk
   * image is unnecessary: zero geometry makes fdd_set_data() stop before
   * dereferencing track storage.
   */
  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {

    jlo.drives[ i ].loaded = 1;

    jlo.drives[ i ].disk.sides = 0;
    jlo.drives[ i ].disk.cylinders = 0;
  }

  /*
   * Pretend the WD1770 motor is already active before B7 changes.
   */
  jlo.fdc->status_register |= WD_FDC_SR_MOTORON;

  /*
   * Select drive B.
   */
  jlo_test_dsel_write( &jlo, 0x02 );

  if( jlo.drives[ JLO_DRIVE_0 ].motoron ||
      !jlo.drives[ JLO_DRIVE_1 ].motoron ||
      jlo.drives[ JLO_DRIVE_2 ].motoron ||
      jlo.drives[ JLO_DRIVE_3 ].motoron ) {

    printf(
      "jlo_drive_change_motor_test: invalid motor state after B7=02\n"
      );
    r++;
  }

  /*
   * Change selection to drive C without changing the controller MOTOR ON
   * status. C must start and B must stop.
   */
  jlo_test_dsel_write( &jlo, 0x04 );

  if( jlo.drives[ JLO_DRIVE_0 ].motoron ||
      jlo.drives[ JLO_DRIVE_1 ].motoron ||
      !jlo.drives[ JLO_DRIVE_2 ].motoron ||
      jlo.drives[ JLO_DRIVE_3 ].motoron ) {

    printf(
      "jlo_drive_change_motor_test: invalid motor state after B7=04\n"
      );
    r++;
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_drive_change_motor_test failed\n" );

  return r;
}


/*
 * Verify that a WD motor-off timeout reaches every JLO drive which was
 * selected while the controller MOTOR ON output was asserted.
 */
static int
jlo_multiple_drive_motor_timeout_test( void )
{
  jlo_test_context jlo;
  int i;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
    jlo.drives[ i ].loaded = 1;
    jlo.drives[ i ].disk.sides = 0;
    jlo.drives[ i ].disk.cylinders = 0;
  }

  jlo.fdc->status_register |= WD_FDC_SR_MOTORON;
  jlo_test_dsel_write( &jlo, 0x0a );

  if( !jlo.drives[ JLO_DRIVE_1 ].motoron ||
      !jlo.drives[ JLO_DRIVE_3 ].motoron ) {
    printf(
      "jlo_multiple_drive_motor_timeout_test: "
      "selected drives did not start\n"
      );
    r++;
  }

  /* Plain D0 schedules the standard WD1770 idle motor timeout. */
  wd_fdc_cr_write( jlo.fdc, 0xd0 );

  if( wdfdc_test_run_event( "WD FDC motor off" ) ) {
    printf(
      "jlo_multiple_drive_motor_timeout_test: "
      "no WD motor-off event was scheduled\n"
      );
    r++;
  } else {
    for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
      if( jlo.drives[ i ].motoron ) {
        printf(
          "jlo_multiple_drive_motor_timeout_test: "
          "drive %c remains on after timeout\n",
          'A' + i
          );
        r++;
      }
    }
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_multiple_drive_motor_timeout_test failed\n" );

  return r;
}


/*
 * Verify JLO's SAFE-compatible dummy Data Register read.
 *
 * While BUSY and without DRQ, the cached data register value must be
 * returned without advancing the WD transfer.
 */
static int
jlo_busy_dummy_data_read_test( void )
{
  jlo_test_context jlo;
  libspectrum_byte value;
  int r = 0;

  wdfdc_test_reset_events();
  jlo_test_init( &jlo );

  jlo.fdc->state = WD_FDC_STATE_READ;
  jlo.fdc->status_register |= WD_FDC_SR_BUSY;

  jlo.fdc->datarq = 0;
  jlo.fdc->data_register = 0x5a;
  jlo.fdc->data_offset = 7;

  value = jlo_test_dr_read( &jlo );

  if( value != 0x5a ) {
    printf(
      "jlo_busy_dummy_data_read_test: value is %02x, expected 5a\n",
      value
      );
    r++;
  }

  if( jlo.fdc->data_offset != 7 ) {
    printf(
      "jlo_busy_dummy_data_read_test: data offset changed to %d, "
      "expected 7\n",
      jlo.fdc->data_offset
      );
    r++;
  }

  if( jlo.fdc->datarq ) {
    printf(
      "jlo_busy_dummy_data_read_test: DRQ changed unexpectedly\n"
      );
    r++;
  }

  jlo_test_end( &jlo );

  if( r )
    printf( "jlo_busy_dummy_data_read_test failed\n" );

  return r;
}


int
main( void )
{
  int r = 0;

  wdfdc_test_machine_init();

  /*
   * Register the real FDD and WD FDC event handlers in the standalone
   * scheduler supplied by wdfdc_fakes.c.
   */
  fdd_register_startup();

  r += jlo_drive_select_test();
  r += jlo_current_drive_retention_test();
  r += jlo_multiple_drive_step_test();
  r += jlo_drive_change_motor_test();
  r += jlo_multiple_drive_motor_timeout_test();
  r += jlo_busy_dummy_data_read_test();

  if( r ) {

    printf(
      "\nJLO standalone tests: %d failure%s\n",
      r,
      r == 1 ? "" : "s"
      );

    return 1;
  }

  printf( "JLO standalone tests: OK\n" );

  return 0;
}
