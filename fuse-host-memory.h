/* fuse-host-memory.h: opt-in host memory and trainer workflows
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

#ifndef FUSE_HOST_MEMORY_H
#define FUSE_HOST_MEMORY_H

#include "fuse-host.h"

/* Operate on the current mapped 64 KiB address space without wrapping.
   Length is nonzero. Copy writes to the caller's buffer. Load uses the
   first length bytes of the file and rejects short input before writing.
   Save uses an authorized target and may overwrite it without a prompt. */
fuse_host_operation_result fuse_host_load_binary( const char *filename,
                                                  size_t start, size_t length );
fuse_host_operation_result fuse_host_save_binary( const char *filename,
                                                  size_t start, size_t length );

typedef enum fuse_poke_filter {
  FUSE_POKE_EXACT, FUSE_POKE_INCREMENTED, FUSE_POKE_DECREMENTED
} fuse_poke_filter;

typedef struct fuse_poke_match {
  size_t bank, offset; /* Physical RAM bank and byte offset. */
} fuse_poke_match;

/* Reset takes a new RAM baseline; machine reset/selection invalidates it.
   Copy returns an independent array (free with libspectrum_free()). */
fuse_host_operation_result fuse_host_reset_poke_finder( void );
fuse_host_operation_result fuse_host_filter_poke_finder(
  fuse_poke_filter filter,
  libspectrum_byte value );
fuse_host_operation_result fuse_host_copy_poke_matches(
  fuse_poke_match **matches,
  size_t *count );

/* Legacy custom POK addressing: banks 0–7, or 8 for mapped RAM. Value 256
   asks for a byte at activation. Creating a trainer does not write memory. */
fuse_host_operation_result fuse_host_add_custom_pok_trainer(
  size_t bank, size_t address, int value );

typedef struct fuse_pok_trainer {
  char *name;
  int active, disabled, asks_value;
} fuse_pok_trainer;

typedef struct fuse_pok_trainers {
  uint64_t generation;
  size_t count;
  fuse_pok_trainer *trainers;
} fuse_pok_trainers;

/* Owned copy of the loaded trainer list. A replacement or reset makes its
   generation stale. Ordinals are zero-based. Pass -1 when no value is needed,
   or 0–255 when enabling a trainer that asks for a value. */
fuse_host_operation_result fuse_host_copy_pok_trainers(
  fuse_pok_trainers **info );
void fuse_pok_trainers_free( fuse_pok_trainers *info );
fuse_host_operation_result fuse_host_set_pok_trainer( uint64_t generation,
                                                      size_t ordinal,
                                                      int enabled, int value );

#endif
