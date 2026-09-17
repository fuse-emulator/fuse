/* automation.h: development-only one-shot automation interface
   Copyright (c) 2026 Fredrick Meunier

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
*/
#ifndef FUSE_AUTOMATION_H
#define FUSE_AUTOMATION_H

#include <stddef.h>
#include "ui/ui.h"

typedef struct automation_scenario {
  char *output_directory;
  unsigned long maximum_frames;
} automation_scenario;

typedef enum automation_termination_type {
  AUTOMATION_TERMINATION_FRAMES,
  AUTOMATION_TERMINATION_ERROR
} automation_termination_type;

typedef struct automation_result {
  unsigned long frames_completed;
  automation_termination_type termination;
} automation_result;

int automation_options_present( int argc, char **argv );
int automation_set_output_directory( const char *directory );
int automation_set_frame_limit( const char *frames );
int automation_validate_scenario( void );
int automation_active( void );
unsigned long automation_maximum_frames( void );
void automation_arm( unsigned long frame_count );
int automation_frame_limit_reached( unsigned long frame_count );
void automation_diagnostic( ui_error_level severity, const char *message );
int automation_write_result( void );
void automation_end( void );

#endif
