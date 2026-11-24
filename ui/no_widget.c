/* No-widget implementations of the legacy UI hooks.
   SPDX-License-Identifier: GPL-2.0-or-later */

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
