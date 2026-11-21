/* fuse-tape.h: owned tape browser and transport observations
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef FUSE_TAPE_INFO_H
#define FUSE_TAPE_INFO_H

#include <stddef.h>

/* Display strings use the existing browser description/detail formatting.
   Both are owned, NUL-terminated UTF-8 (description labels are ASCII).
   Empty details are valid; this is not raw metadata or a block-type identity. */
typedef struct fuse_tape_block_info {
  char *description;
  char *details;
} fuse_tape_block_info;

typedef struct fuse_tape_info {
  int present;
  int playing;
  int recording;
  int modified;
  int current_block; /* Zero-based, -1 if none, as in the existing browser. */
  size_t block_count;
  fuse_tape_block_info *blocks; /* Ordered rows; NULL when block_count is zero. */
} fuse_tape_info;

/* Frees all strings, rows and the copy; NULL-safe and valid after shutdown. */
void fuse_tape_info_free( fuse_tape_info *info );

#endif /* FUSE_TAPE_INFO_H */
