/* fuse.h: Variables exported from the main file
   Copyright (c) 2000-2015 Philip Kendall

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Author contact information:

   E-mail: philip-fuse@shadowmagic.org.uk

*/

#ifndef FUSE_FUSE_H
#define FUSE_FUSE_H

#include <limits.h>

#include "libspectrum.h"

#include "compat.h"

#define FUSE_APP_ID "net.sourceforge.fuse_emulator.Fuse"

extern const char *fuse_progname;	/* argv[0] */

extern int fuse_exiting;		/* Shall we exit now? */

extern int fuse_emulation_paused;	/* Is Spectrum emulation paused? */
int fuse_emulation_pause( void );
int fuse_emulation_unpause( void );

#ifdef UI_WIN32
int fuse_main(int argc, char **argv);
#endif

void fuse_abort( void ) GCC_NORETURN;	/* Emergency shutdown */

extern libspectrum_creator *fuse_creator; /* Creator information for file
					     formats which support this */

/* Engine implementation hooks; not part of the host API. */
/* Coordinate changes preserve elapsed time; replacement interrupts an active
   budget. These do nothing during frame/legacy execution or between turns. */
void fuse_budget_rebase( unsigned long before, unsigned long after );
void fuse_budget_interrupt( void );

/* Record reported ROM errors during host preparation/mutations, including
   legacy void callbacks which disable their peripheral. Not fatal recovery. */
void fuse_host_resource_error( void );

#endif			/* #ifndef FUSE_FUSE_H */
