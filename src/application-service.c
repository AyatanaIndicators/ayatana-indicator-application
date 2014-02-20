/*
The core file for the service that starts up all the objects we need
and houses our main loop.

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


#include "libindicator/indicator-service.h"
#include "application-service-appstore.h"
#include "application-service-watcher.h"
#include "dbus-shared.h"

/* The base main loop */
static GMainLoop * mainloop = NULL;
/* Where the application registry lives */
static ApplicationServiceAppstore * appstore = NULL;
/* Interface for applications */
static ApplicationServiceWatcher * watcher = NULL;

/* Builds up the core objects and puts us spinning into
   a main loop. */
int
main (int argc, char ** argv)
{
	/* Building our app store */
	appstore = application_service_appstore_new();

	/* Adding a watcher for the Apps coming up */
	watcher = application_service_watcher_new(appstore);

	/* Building and executing our main loop */
	mainloop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(mainloop);

	/* Unref'ing all the objects */
	g_object_unref(G_OBJECT(watcher));
	g_object_unref(G_OBJECT(appstore));

	return 0;
}
