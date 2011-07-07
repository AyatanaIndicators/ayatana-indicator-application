#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "dbus-shared.h"
#include "libappindicator/app-indicator.h"
#include "gen-notification-approver.xml.h"

#define APPROVER_PATH  "/my/approver"

#define INDICATOR_ID        "test-indicator-id"
#define INDICATOR_ICON      "test-indicator-icon-name"
#define INDICATOR_CATEGORY  APP_INDICATOR_CATEGORY_APPLICATION_STATUS

#define TEST_APPROVER_TYPE            (test_approver_get_type ())
#define TEST_APPROVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TEST_APPROVER_TYPE, TestApprover))
#define TEST_APPROVER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TEST_APPROVER_TYPE, TestApproverClass))
#define IS_TEST_APPROVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TEST_APPROVER_TYPE))
#define IS_TEST_APPROVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TEST_APPROVER_TYPE))
#define TEST_APPROVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_APPROVER_TYPE, TestApproverClass))

typedef struct _TestApprover      TestApprover;
typedef struct _TestApproverClass TestApproverClass;

struct _TestApproverClass {
	GObjectClass parent_class;
};

struct _TestApprover {
	GObject parent;
};

GType test_approver_get_type (void);

static void test_approver_class_init (TestApproverClass *klass);
static void test_approver_init       (TestApprover *self);
static GVariant * approve_item (TestApprover * ta, const gchar * id);
static void bus_method_call (GDBusConnection * connection, const gchar * sender, const gchar * path, const gchar * interface, const gchar * method, GVariant * params, GDBusMethodInvocation * invocation, gpointer user_data);

/* GDBus Stuff */
static GDBusNodeInfo *      node_info = NULL;
static GDBusInterfaceInfo * interface_info = NULL;
static GDBusInterfaceVTable interface_table = {
       method_call:    bus_method_call,
       get_property:   NULL, /* No properties */
       set_property:   NULL  /* No properties */
};

GMainLoop * main_loop = NULL;
GDBusConnection * session_bus = NULL;
GDBusProxy * bus_proxy = NULL;
GDBusProxy * watcher_proxy = NULL;
AppIndicator * app_indicator = NULL;
gboolean passed = FALSE;

G_DEFINE_TYPE (TestApprover, test_approver, G_TYPE_OBJECT);

static void
test_approver_class_init (TestApproverClass *klass)
{
	/* Setting up the DBus interfaces */
	if (node_info == NULL) {
		GError * error = NULL;

		node_info = g_dbus_node_info_new_for_xml(_notification_approver, &error);
		if (error != NULL) {
			g_error("Unable to parse Approver Service Interface description: %s", error->message);
			g_error_free(error);
		}
	}

	if (interface_info == NULL) {
		interface_info = g_dbus_node_info_lookup_interface(node_info, NOTIFICATION_APPROVER_DBUS_IFACE);

		if (interface_info == NULL) {
			g_error("Unable to find interface '" NOTIFICATION_APPROVER_DBUS_IFACE "'");
		}
	}

	return;
}

static void
test_approver_init (TestApprover *self)
{
	GError * error = NULL;

	/* Now register our object on our new connection */
	g_dbus_connection_register_object(session_bus,
	                                  APPROVER_PATH,
	                                  interface_info,
	                                  &interface_table,
	                                  self,
	                                  NULL,
	                                  &error);

	if (error != NULL) {
		g_error("Unable to register the object to DBus: %s", error->message);
		g_error_free(error);
		return;
	}

	return;
}

static GVariant *
approve_item (TestApprover * ta, const gchar * id)
{
	g_debug("Asked to approve indicator");

	if (g_strcmp0(id, INDICATOR_ID) == 0) {
		passed = TRUE;
	}

	g_main_loop_quit(main_loop);

	return g_variant_new("(b)", TRUE);
}

/* A method has been called from our dbus inteface.  Figure out what it
   is and dispatch it. */
static void
bus_method_call (GDBusConnection * connection, const gchar * sender,
                 const gchar * path, const gchar * interface,
                 const gchar * method, GVariant * params,
                 GDBusMethodInvocation * invocation, gpointer user_data)
{
	TestApprover * ta = (TestApprover *)user_data;
	GVariant * retval = NULL;

	if (g_strcmp0(method, "ApproveItem") == 0) {
		const gchar * id;
		g_variant_get(params, "(&ssuso)", &id, NULL, NULL, NULL, NULL);
		retval = approve_item(ta, id);
	} else {
		g_warning("Calling method '%s' on the indicator service and it's unknown", method);
	}

	g_dbus_method_invocation_return_value(invocation, retval);
	return;
}

static void
register_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy * proxy = G_DBUS_PROXY(object);
	GError * error = NULL;
	GVariant * result;

	result = g_dbus_proxy_call_finish(proxy, res, &error);

	if (result != NULL) {
		g_variant_unref(result);
		result = NULL;
	}

	if (error != NULL) {
		g_warning("Unable to register approver: %s", error->message);
		g_error_free(error);
		g_main_loop_quit(main_loop);
		return;
	}

	g_debug("Building App Indicator");
	app_indicator = app_indicator_new(INDICATOR_ID, INDICATOR_ICON, INDICATOR_CATEGORY);

	GtkWidget * menu = gtk_menu_new();
	GtkWidget * mi = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT, NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);

	app_indicator_set_menu(app_indicator, GTK_MENU(menu));

	return;
}

gint owner_count = 0;
gboolean
check_for_service (gpointer user_data)
{
	g_debug("Checking for Watcher");

	if (owner_count > 100) {
		g_warning("Couldn't find watcher after 100 tries.");
		g_main_loop_quit(main_loop);
		return FALSE;
	}

	owner_count++;

	gboolean has_owner = FALSE;
	gchar * owner = g_dbus_proxy_get_name_owner(bus_proxy);
	has_owner = (owner != NULL);
	g_free (owner);

	if (has_owner) {
		g_debug("Registering Approver");
		GVariantBuilder * builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
		g_dbus_proxy_call(bus_proxy, "XAyatanaRegisterNotificationApprover",
		                  g_variant_new("(oas)", APPROVER_PATH, builder),
		                  G_DBUS_CALL_FLAGS_NONE, -1, NULL, register_cb,
		                  NULL);
		return FALSE;
	}

	return TRUE;
}

gboolean
fail_timeout (gpointer user_data)
{
	g_debug("Failure timeout initiated.");
	g_main_loop_quit(main_loop);
	return FALSE;
}

int
main (int argc, char ** argv)
{
	GError * error = NULL;

	gtk_init(&argc, &argv);
	g_debug("Initing");

	session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	TestApprover * approver = g_object_new(TEST_APPROVER_TYPE, NULL);

	bus_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, NULL, NOTIFICATION_WATCHER_DBUS_ADDR, NOTIFICATION_WATCHER_DBUS_OBJ, NOTIFICATION_WATCHER_DBUS_IFACE, NULL, &error);
	if (error != NULL) {
		g_warning("Unable to get bus proxy: %s", error->message);
		g_error_free(error);
		return -1;
	}

	watcher_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, NULL, NOTIFICATION_WATCHER_DBUS_ADDR, NOTIFICATION_WATCHER_DBUS_OBJ, NOTIFICATION_WATCHER_DBUS_IFACE, NULL, &error);
	if (error != NULL) {
		g_warning("Unable to get watcher bus: %s", error->message);
		g_error_free(error);
		return -1;
	}

	g_timeout_add(100, check_for_service, NULL);
	g_timeout_add_seconds(2, fail_timeout, NULL);

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	g_object_unref(approver);

	if (!passed) {
		return -1;
	}

	return 0;
}
