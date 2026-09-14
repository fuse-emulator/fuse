/* tape_internals.h: internal tape interfaces
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#ifndef FUSE_TAPE_INTERNALS_H
#define FUSE_TAPE_INTERNALS_H

#include "libspectrum.h"

extern libspectrum_tape *tape;
extern int trap_resume_pending;

int tape_autoload( libspectrum_machine hardware );
int tape_play( int autoplay );
libspectrum_error tape_trap_finish_rom_block( void );
void tape_update_microphone( const libspectrum_tape_edge *edge );

void tape_record_init( libspectrum_tape *current_tape );

#endif
