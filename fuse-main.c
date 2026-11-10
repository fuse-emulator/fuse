/* fuse-main.c: application entry and platform bootstrap
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

#ifdef WIN32
#include <windows.h>
#endif

/* SDL may redefine main on macOS and Windows, including when a GTK/Xlib
   frontend uses SDL joystick support. Keep that bootstrap out of the engine. */
#if defined UI_SDL2 || (defined USE_JOYSTICK && (defined UI_X || defined UI_GTK))
#include <SDL.h>
#endif

#ifdef GEKKO
/* <fat.h> was not included by the legacy entry: some Wii SDKs lack the
   disc_io.h it requires. Preserve that bootstrap arrangement here. */
#endif

#include "fuse.h"
#include "fuse-internal.h"

#ifdef UI_WIN32
int fuse_main( int argc, char **argv )
#else
int main( int argc, char **argv )
#endif
{
#ifdef WIN32
  SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
#endif

#ifdef GEKKO
  fatInitDefault();
#endif

  return fuse_legacy_main( argc, argv );
}
