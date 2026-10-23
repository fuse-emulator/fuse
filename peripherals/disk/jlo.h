/* jlo.h: JLO Disk System interface */

#ifndef FUSE_JLO_H
#define FUSE_JLO_H

#include "libspectrum.h"
#include "peripherals/disk/fdd.h"

typedef enum jlo_drive_number {
  JLO_DRIVE_0 = 0,
  JLO_DRIVE_1,
  JLO_DRIVE_2,
  JLO_DRIVE_3,
  JLO_NUM_DRIVES
} jlo_drive_number;

extern int jlo_available;
extern int jlo_active;

void jlo_register_startup( void );

void jlo_page( void );
void jlo_unpage( void );

int jlo_disk_insert( jlo_drive_number which, const char *filename,
                     int autoload );
fdd_t *jlo_get_fdd( jlo_drive_number which );

#endif /* #ifndef FUSE_JLO_H */
