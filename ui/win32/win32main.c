/* win32main.c: Win32 application entry
   Copyright (c) 2003-2015 Marek Januszewski, Philip Kendall, Stuart Brady
   Copyright (c) 2015 Kirben
   Copyright (c) 2016 lordhoto
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
#include <stdlib.h>

#include "fuse.h"
#include "win32internals.h"

int WINAPI
WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
         int nCmdShow )
{
  win32ui_set_application( hInstance, hPrevInstance, nCmdShow );

/* HACK: __argc, __argv are broken and return zero when using mingwrt 4.0+
   on MinGW.
   HACK: MinGW-w64 based toolchains neither feature _argc nor _argv. The 32 bit
   incarnation only defines __MINGW32__. This leads to build breakage due to
   missing declarations. Luckily MinGW-w64 based toolchains define
   __MINGW64_VERSION_foo macros inside _mingw.h, which is included from all
   system headers. Thus we abuse that to detect them.
*/
#if defined( __GNUC__ ) && defined( __MINGW32__ ) \
                        && !defined( __MINGW64_VERSION_MAJOR )
  return fuse_main( _argc, _argv );
#else
  return fuse_main( __argc, __argv );
#endif
}
