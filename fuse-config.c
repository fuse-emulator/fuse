/* fuse-config.c: engine-owned opaque configuration and defaults
   Copyright (c) 1999-2026 Philip Kendall and others
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

#include "config.h"

#include "fuse-engine.h"
#include "settings.h"
#include "utils.h"

struct fuse_host_config {
  char *machine;
  int sound;
  int auto_load;
  int tape_traps;
  int divmmc_enabled;
  int interface1;
  joystick_type_t joystick_output[3];
  int mouse_swap_buttons;
};

fuse_host_config *
fuse_host_config_alloc( void )
{
  settings_info defaults = { 0 };
  fuse_host_config *config = libspectrum_new0( fuse_host_config, 1 );
  settings_defaults( &defaults );
  config->machine = utils_safe_strdup( defaults.start_machine );
  config->sound = defaults.sound;
  config->auto_load = defaults.auto_load;
  config->tape_traps = defaults.tape_traps;
  config->divmmc_enabled = defaults.divmmc_enabled;
  config->interface1 = defaults.interface1;
  config->joystick_output[0] = defaults.joystick_1_output;
  config->joystick_output[1] = defaults.joystick_2_output;
  config->joystick_output[2] = defaults.joystick_keyboard_output;
  config->mouse_swap_buttons = defaults.mouse_swap_buttons;
  settings_free( &defaults );
  return config;
}

void
fuse_host_config_free( fuse_host_config *config )
{
  if( !config ) return;
  libspectrum_free( config->machine );
  libspectrum_free( config );
}

int
fuse_host_config_set_machine( fuse_host_config *config, const char *id )
{
  if( !config || !id || !*id ) return 1;
  settings_set_string( &config->machine, id );
  return 0;
}

int
fuse_host_config_set_sound( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->sound = enabled;
  return 0;
}

int
fuse_host_config_set_auto_load( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->auto_load = enabled;
  return 0;
}

int
fuse_host_config_set_tape_traps( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->tape_traps = enabled;
  return 0;
}

int
fuse_host_config_set_divmmc( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->divmmc_enabled = enabled;
  return 0;
}

int
fuse_host_config_set_interface1( fuse_host_config *config, int enabled )
{
  if( !config || ( enabled != 0 && enabled != 1 ) ) return 1;
  config->interface1 = enabled;
  return 0;
}

int
fuse_host_config_set_joystick( fuse_host_config *config,
  fuse_host_joystick slot, joystick_type_t output )
{
  if( !config || slot < FUSE_HOST_JOYSTICK_1 || slot > FUSE_HOST_JOYSTICK_KEYBOARD ||
      output < JOYSTICK_TYPE_NONE || output >= JOYSTICK_TYPE_COUNT )
    return 1;
  config->joystick_output[slot] = output;
  return 0;
}

int
fuse_host_config_set_mouse_swap_buttons( fuse_host_config *config, int swap )
{
  if( !config || ( swap != 0 && swap != 1 ) ) return 1;
  config->mouse_swap_buttons = swap;
  return 0;
}

void
fuse_engine_apply_config( const fuse_host_config *config )
{
  settings_set_string( &settings_current.start_machine, config->machine );
  settings_current.sound = config->sound;
  settings_current.auto_load = config->auto_load;
  settings_current.tape_traps = config->tape_traps;
  settings_current.divmmc_enabled = config->divmmc_enabled;
  settings_current.interface1 = config->interface1;
  settings_current.joystick_1_output = config->joystick_output[0];
  settings_current.joystick_2_output = config->joystick_output[1];
  settings_current.joystick_keyboard_output = config->joystick_output[2];
  settings_current.mouse_swap_buttons = config->mouse_swap_buttons;
}
