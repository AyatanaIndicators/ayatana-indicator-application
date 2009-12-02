/*
A small piece of sample code demonstrating a very simple application
with an indicator.

Copyright 2009 Canonical Ltd.

Authors:
    Ted Gould <ted@canonical.com>

This program is free software: you can redistribute it and/or modify it 
under the terms of the GNU General Public License version 3, as published 
by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but 
WITHOUT ANY WARRANTY; without even the implied warranties of 
MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR 
PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along 
with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "libappindicator/app-indicator.h"
#include "libdbusmenu-glib/server.h"
#include "libdbusmenu-glib/menuitem.h"

GMainLoop * mainloop = NULL;

int
main (int argc, char ** argv)
{
	g_type_init();

	AppIndicator * ci = APP_INDICATOR(g_object_new(APP_INDICATOR_TYPE, NULL));
	g_assert(ci != NULL);

	app_indicator_set_id(ci, "example-simple-client");
	app_indicator_set_category(ci, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
	app_indicator_set_status(ci, APP_INDICATOR_STATUS_ACTIVE);
	app_indicator_set_icon(ci, "indicator-messages");
	app_indicator_set_attention_icon(ci, "indicator-messages-new");

	DbusmenuMenuitem * root = dbusmenu_menuitem_new();

	DbusmenuMenuitem * item = dbusmenu_menuitem_new();
	dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_LABEL, "Item 1");
	dbusmenu_menuitem_child_append(root, item);

	item = dbusmenu_menuitem_new();
	dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_LABEL, "Item 2");
	dbusmenu_menuitem_child_append(root, item);

	DbusmenuServer * menuservice = dbusmenu_server_new ("/need/a/menu/path");
	dbusmenu_server_set_root(menuservice, root);

	app_indicator_set_menu(ci, menuservice);

	mainloop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(mainloop);

	return 0;
}
