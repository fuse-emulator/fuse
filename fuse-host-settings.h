/* fuse-host-settings.h: runtime settings source interface
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

#ifndef FUSE_HOST_SETTINGS_H
#define FUSE_HOST_SETTINGS_H

#include "fuse-host.h"
#include "settings.h"

/* Prepare from an initialized record, copied before machine initialization.
   No settings file, command line or legacy UI initialization is performed.
   Startup media paths, communication endpoints and debugger commands are
   rejected before consuming the preparation attempt. Attach media explicitly
   after preparation. CLI help/version and internal-test flags are ignored.
   The ROM provider receives the configured ROM names. */
fuse_init_result fuse_host_prepare_settings(
  const settings_info *settings, const fuse_host_resources *resources );

/* Matching generated settings.h required. Free the owned copy with the
   function below. Runtime emulator options may change; startup fields and
   adapter-owned presentation and persistence fields must remain unchanged. */
fuse_host_operation_result fuse_host_copy_settings( settings_info **copy );
void fuse_host_free_settings( settings_info *copy );
/* NEEDS_RESET leaves settings unchanged. Resubmit with allow_reset = 1. */
fuse_host_operation_result fuse_host_apply_settings(
  const settings_info *candidate, int allow_reset );

#endif
