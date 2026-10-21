/* jlo_logic.c: testable JLO Disk System drive wiring */

#include "config.h"

#include "jlo_logic.h"

void
jlo_logic_motor( fdd_t drives[ JLO_NUM_DRIVES ],
                 libspectrum_byte dsel, int on )
{
  int i;

  /*
   * The WD1770 has one MOTOR ON output; Board B distributes it through
   * external drive-selection logic. While MOTOR ON is asserted, only the
   * currently selected JLO drives run. When it is deasserted, stop every
   * JLO drive so no stale motor state can survive a selection change.
   */
  for( i = 0; i < JLO_NUM_DRIVES; i++ )
    fdd_motoron( &drives[ i ], on && ( dsel & ( 1 << i ) ) );
}

void
jlo_logic_step( fdd_t drives[ JLO_NUM_DRIVES ],
                libspectrum_byte dsel, int direction )
{
  int i;

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
    if( dsel & ( 1 << i ) )
      fdd_step( &drives[ i ], direction );
  }
}

void
jlo_logic_select( wd_fdc *fdc, fdd_t drives[ JLO_NUM_DRIVES ],
                  fdd_t *unselected_drive, libspectrum_byte *dsel,
                  libspectrum_byte value )
{
  int i;
  int side = ( value & 0x80 ) ? 1 : 0;
  int motor = !!( fdc->status_register & WD_FDC_SR_MOTORON );
  int current_selected = 0;

  *dsel = value;

  /* B7 bit 6 drives /DDEN: 0 selects MFM, 1 selects FM. The WD core's
     dden field is logical MFM enable, not the physical active-low pin. */
  fdc->dden = ( value & 0x40 ) ? 0 : 1;

  for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
    int selected = ( value & ( 1 << i ) ) ? 1 : 0;

    fdd_set_head( &drives[ i ], side );
    fdd_select( &drives[ i ], selected );

    if( selected && fdc->current_drive == &drives[ i ] )
      current_selected = 1;
  }

  if( !current_selected ) {
    /*
     * Board B permits all drive-select lines to be inactive. Keep the WD
     * core's current_drive pointer valid without pretending drive A is
     * selected by using an unloaded dummy drive for that electrical state.
     */
    fdc->current_drive = unselected_drive;

    for( i = 0; i < JLO_NUM_DRIVES; i++ ) {
      if( value & ( 1 << i ) ) {
        fdc->current_drive = &drives[ i ];
        break;
      }
    }
  }

  /* Reproject the existing WD MOTOR ON output onto the new selection. */
  jlo_logic_motor( drives, *dsel, motor );
}

libspectrum_byte
jlo_logic_data_read( wd_fdc *fdc )
{
  /*
   * SAFE performs dummy reads while a multisector command is BUSY but DRQ
   * is inactive. Return the cached byte without consuming another byte or
   * cancelling the pending FDC event.
   */
  if( ( fdc->status_register & WD_FDC_SR_BUSY ) && !fdc->datarq )
    return fdc->data_register;

  return wd_fdc_dr_read( fdc );
}
