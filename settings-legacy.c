/* settings-legacy.c: legacy settings storage and application policy
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

#define FUSE_LEGACY_SETTINGS
#include "settings.c"

#include "fuse-engine.h"
#include "fuse-legacy.h"

void
settings_end( int save )
{
  if( save && settings_current_is_owned() && settings_current.autosave_settings )
    settings_write_config( &settings_current );
  settings_dispose_current();
}

fuse_init_result
fuse_legacy_prepare( int argc, char **argv )
{
  return fuse_engine_prepare_legacy( argc, argv, settings_init,
                                     settings_write_config, 0 );
}

int
fuse_legacy_main( int argc, char **argv )
{
  int r = 1;
  switch( fuse_engine_prepare_legacy( argc, argv, settings_init,
                                      settings_write_config, 1 ) ) {
  case FUSE_INIT_READY:
    if( fuse_legacy_execute( &r ) ) r = 1;
    fuse_shutdown();
    return r;
  case FUSE_INIT_HELP:
  case FUSE_INIT_VERSION:
    return 0;
  case FUSE_INIT_BUSY:
  case FUSE_INIT_CLOSED:
    fprintf( stderr, "Fuse legacy runner supports only one session attempt\n" );
    return 1;
  case FUSE_INIT_INVALID_ARGUMENT:
    fprintf( stderr, "Invalid Fuse legacy startup arguments\n" );
    return 1;
  case FUSE_INIT_FAILED:
    return 1;
  }
  return 1;
}
