/* fuse-legacy.h: private legacy application adapter
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

#ifndef FUSE_LEGACY_H
#define FUSE_LEGACY_H

#include "fuse-internal.h"

/* Prepare using application configuration, CLI parsing and startup files.
   argv and its mutable strings remain borrowed through shutdown. argc must be
   nonnegative; argc == 0 with argv == NULL is permitted. Otherwise argv contains
   non-NULL entries followed by a NULL terminator. Help/version and failure clean
   up without a runnable session; accepted attempts consume the shared lifetime.
   Preparation alone never authorizes automatic preference saving. */
fuse_init_result fuse_legacy_prepare( int argc, char **argv );

/* Run the blocking legacy loop once after READY, optionally after host turns.
   Zero means accepted; the application exit status is written separately.
   Rejection (including a pending host decision) leaves it alone. This
   finalizes application automation output once;
   subsequent execution is rejected. Not cooperative execution. */
int fuse_legacy_execute( int *exit_status );

/* Compatible application runner, without a platform entry point. Authorizes
   the existing autosave preference behaviour only after successful preparation. */
int fuse_legacy_main( int argc, char **argv );

#endif
