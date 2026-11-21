/* tapesaveui.c: compile the original UI helper with test-only service doubles
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
#define tape_write test_tape_save_write
#define tape_resolve_eject test_tape_resolve_eject
#define tape_finish_eject test_tape_finish_eject
#define ui_get_save_filename test_tape_save_filename
#define ui_confirm_save_specific test_tape_save_confirm
#include "../ui.c"
