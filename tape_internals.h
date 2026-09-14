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

void tape_record_init( libspectrum_tape *current_tape );

#endif
