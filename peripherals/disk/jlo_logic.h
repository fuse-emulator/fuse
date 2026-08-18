/* jlo_logic.h: testable JLO Disk System drive wiring */

#ifndef FUSE_JLO_LOGIC_H
#define FUSE_JLO_LOGIC_H

#include "libspectrum.h"

#include "peripherals/disk/fdd.h"
#include "peripherals/disk/jlo.h"
#include "peripherals/disk/wd_fdc.h"

void jlo_logic_select( wd_fdc *fdc, fdd_t drives[ JLO_NUM_DRIVES ],
                       fdd_t *unselected_drive, libspectrum_byte *dsel,
                       libspectrum_byte value );
void jlo_logic_motor( fdd_t drives[ JLO_NUM_DRIVES ],
                      libspectrum_byte dsel, int on );
void jlo_logic_step( fdd_t drives[ JLO_NUM_DRIVES ],
                     libspectrum_byte dsel, int direction );
libspectrum_byte jlo_logic_data_read( wd_fdc *fdc );

#endif
