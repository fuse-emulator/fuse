/* floppysaveui.c: original shared floppy workflow with test-only services
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
#define disk_write test_floppy_disk_write
#define utils_file_read test_floppy_file_read
#define utils_file_free test_floppy_file_free
#define ui_get_save_filename test_floppy_save_filename
#define ui_confirm_save test_floppy_save_confirm
#include "../uimedia.c"
#include "../peripherals/disk/drive.c"
