/* no_widget.c: no-widget legacy UI hooks
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

#include "ui/ui.h"

const int ui_has_widget_menu = 0;

int
ui_widget_init( void )
{
  return 0;
}

int
ui_widget_end( void )
{
  return 0;
}

void
ui_widget_finish( void )
{
}

void
ui_popup_menu( int native_key GCC_UNUSED )
{
}

void
ui_widget_keyhandler( int native_key GCC_UNUSED )
{
}
