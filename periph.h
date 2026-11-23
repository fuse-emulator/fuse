/* periph.h: code for handling peripherals
   Copyright (c) 2004-2023 Philip Kendall, Stuart Brady, Gergely Szasz,
     Alistair Cree

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

#ifndef FUSE_PERIPH_H
#define FUSE_PERIPH_H

#include "libspectrum.h"

#include "fuse-peripheral.h"

/*
 * General peripheral list handling routines
 */

/* For indicating the (optional) presence or absence of a peripheral */
typedef enum periph_present {
  PERIPH_PRESENT_NEVER,		/* Never present */
  PERIPH_PRESENT_OPTIONAL,	/* Optionally present */
  PERIPH_PRESENT_ALWAYS,	/* Always present */
} periph_present;

typedef libspectrum_byte (*periph_port_read_function)( libspectrum_word port,
						       libspectrum_byte *attached );
typedef void (*periph_port_write_function)( libspectrum_word port,
					    libspectrum_byte data );

/* Information about a specific port response */
typedef struct periph_port_t {

  /* This peripheral responds to all port values where
     <port> & mask == value */
  libspectrum_word mask;
  libspectrum_word value;

  periph_port_read_function read;
  periph_port_write_function write;

} periph_port_t;

typedef void (*periph_activate_function)( void );

/* Information about a peripheral */
typedef struct periph_t {
  /* The preferences option which controls this peripheral */
  int *option;
  /* The list of ports this peripheral responds to */
  const periph_port_t *ports;
  /* Hard reset required when added/removed */
  int hard_reset;
  /* Function to be called when the peripheral is activated */
  periph_activate_function activate;
} periph_t;

/* Register a peripheral with the system */
void periph_register( periph_type type, const periph_t *periph );

/* Set whether a peripheral can be present on this machine or not */
void periph_set_present( periph_type type, periph_present present );

/* Mark a specific peripheral as (in)active, returns 1 if the enabled state has
   changed */
int periph_activate_type( periph_type type, int active );

/* Is a specific peripheral active at the moment? */
int periph_is_active( periph_type type );

/* Empty out the list of peripherals */
void periph_clear( void );

/* Free up peripherals */
void periph_end( void );

/*
 * The actual routines to read and write a port
 */

libspectrum_byte readport( libspectrum_word port );
libspectrum_byte readport_internal( libspectrum_word port );

void writeport( libspectrum_word port, libspectrum_byte b );
void writeport_internal( libspectrum_word port, libspectrum_byte b );

/*
 * The more Fuse-specific peripheral handling routines
 */

int periph_update( void );

void periph_posthook( void );

int periph_postcheck( void );

void periph_disable_optional( void );

/* Register debugger page/unpage events for a peripheral */
void periph_register_paging_events( const char *type_string, int *page_event,
				    int *unpage_event );

libspectrum_byte periph_merge_floating_bus( libspectrum_byte value,
                                            libspectrum_byte attached,
                                            libspectrum_byte floating_bus );

#endif				/* #ifndef FUSE_PERIPH_H */
