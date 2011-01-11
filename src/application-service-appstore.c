/*
An object that stores the registration of all the application
indicators.  It also communicates this to the indicator visualization.

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "libappindicator/app-indicator.h"
#include "app-indicator-enum-types.h"
#include "application-service-appstore.h"
#include "application-service-marshal.h"
#include "dbus-shared.h"
#include "generate-id.h"

/* DBus Prototypes */
static GVariant * get_applications (ApplicationServiceAppstore * appstore);
static void bus_method_call (GDBusConnection * connection, const gchar * sender, const gchar * path, const gchar * interface, const gchar * method, GVariant * params, GDBusMethodInvocation * invocation, gpointer user_data);

#include "gen-application-service.xml.h"

#define NOTIFICATION_ITEM_PROP_ID                    "Id"
#define NOTIFICATION_ITEM_PROP_CATEGORY              "Category"
#define NOTIFICATION_ITEM_PROP_STATUS                "Status"
#define NOTIFICATION_ITEM_PROP_ICON_NAME             "IconName"
#define NOTIFICATION_ITEM_PROP_AICON_NAME            "AttentionIconName"
#define NOTIFICATION_ITEM_PROP_ICON_THEME_PATH       "IconThemePath"
#define NOTIFICATION_ITEM_PROP_MENU                  "Menu"
#define NOTIFICATION_ITEM_PROP_LABEL                 "XAyatanaLabel"
#define NOTIFICATION_ITEM_PROP_LABEL_GUIDE           "XAyatanaLabelGuide"
#define NOTIFICATION_ITEM_PROP_ORDERING_INDEX        "XAyatanaOrderingIndex"

#define NOTIFICATION_ITEM_SIG_NEW_ICON               "NewIcon"
#define NOTIFICATION_ITEM_SIG_NEW_AICON              "NewAttentionIcon"
#define NOTIFICATION_ITEM_SIG_NEW_STATUS             "NewStatus"
#define NOTIFICATION_ITEM_SIG_NEW_LABEL              "XAyatanaNewLabel"
#define NOTIFICATION_ITEM_SIG_NEW_ICON_THEME_PATH    "NewIconThemePath"

#define OVERRIDE_GROUP_NAME                          "Ordering Index Overrides"
#define OVERRIDE_FILE_NAME                           "ordering-override.keyfile"

/* Private Stuff */
struct _ApplicationServiceAppstorePrivate {
	GCancellable * bus_cancel;
	GDBusConnection * bus;
	guint dbus_registration;
	GList * applications;
	GList * approvers;
	GHashTable * ordering_overrides;
};

typedef enum {
	VISIBLE_STATE_HIDDEN,
	VISIBLE_STATE_SHOWN
} visible_state_t;

#define STATE2STRING(x)  ((x) == VISIBLE_STATE_HIDDEN ? "hidden" : "visible")

typedef struct _Approver Approver;
struct _Approver {
	ApplicationServiceAppstore * appstore; /* not ref'd */
	GCancellable * proxy_cancel;
	GDBusProxy * proxy;
};

typedef struct _Application Application;
struct _Application {
	gchar * id;
	gchar * category;
	gchar * dbus_name;
	gchar * dbus_object;
	ApplicationServiceAppstore * appstore; /* not ref'd */
	GCancellable * dbus_proxy_cancel;
	GDBusProxy * dbus_proxy;
	gboolean validated; /* Whether we've gotten all the parameters and they look good. */
	AppIndicatorStatus status;
	gchar * icon;
	gchar * aicon;
	gchar * menu;
	gchar * icon_theme_path;
	gchar * label;
	gchar * guide;
	gboolean currently_free;
	guint ordering_index;
	GList * approved_by;
	visible_state_t visible_state;
};

#define APPLICATION_SERVICE_APPSTORE_GET_PRIVATE(o) \
			(G_TYPE_INSTANCE_GET_PRIVATE ((o), APPLICATION_SERVICE_APPSTORE_TYPE, ApplicationServiceAppstorePrivate))

/* GDBus Stuff */
static GDBusNodeInfo *      node_info = NULL;
static GDBusInterfaceInfo * interface_info = NULL;
static GDBusInterfaceVTable interface_table = {
       method_call:    bus_method_call,
       get_property:   NULL, /* No properties */
       set_property:   NULL  /* No properties */
};

/* GObject stuff */
static void application_service_appstore_class_init (ApplicationServiceAppstoreClass *klass);
static void application_service_appstore_init       (ApplicationServiceAppstore *self);
static void application_service_appstore_dispose    (GObject *object);
static void application_service_appstore_finalize   (GObject *object);
static gint app_sort_func (gconstpointer a, gconstpointer b, gpointer userdata);
static void load_override_file (GHashTable * hash, const gchar * filename);
static AppIndicatorStatus string_to_status(const gchar * status_string);
static void apply_status (Application * app);
static AppIndicatorCategory string_to_cat(const gchar * cat_string);
static void approver_free (gpointer papprover, gpointer user_data);
static void check_with_new_approver (gpointer papp, gpointer papprove);
static void check_with_old_approver (gpointer papprove, gpointer papp);
static Application * find_application (ApplicationServiceAppstore * appstore, const gchar * address, const gchar * object);
static void bus_get_cb (GObject * object, GAsyncResult * res, gpointer user_data);
static void dbus_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data);
static void app_receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name, GVariant * parameters, gpointer user_data);
static void approver_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data);
static void approver_receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name, GVariant * parameters, gpointer user_data);

G_DEFINE_TYPE (ApplicationServiceAppstore, application_service_appstore, G_TYPE_OBJECT);

static void
application_service_appstore_class_init (ApplicationServiceAppstoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ApplicationServiceAppstorePrivate));

	object_class->dispose = application_service_appstore_dispose;
	object_class->finalize = application_service_appstore_finalize;

	/* Setting up the DBus interfaces */
	if (node_info == NULL) {
		GError * error = NULL;

		node_info = g_dbus_node_info_new_for_xml(_application_service, &error);
		if (error != NULL) {
			g_error("Unable to parse Application Service Interface description: %s", error->message);
			g_error_free(error);
		}
	}

	if (interface_info == NULL) {
		interface_info = g_dbus_node_info_lookup_interface(node_info, INDICATOR_APPLICATION_DBUS_IFACE);

		if (interface_info == NULL) {
			g_error("Unable to find interface '" INDICATOR_APPLICATION_DBUS_IFACE "'");
		}
	}

	return;
}

static void
application_service_appstore_init (ApplicationServiceAppstore *self)
{
    
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE_GET_PRIVATE (self);

	priv->applications = NULL;
	priv->approvers = NULL;
	priv->bus_cancel = NULL;
	priv->dbus_registration = 0;

	priv->ordering_overrides = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	load_override_file(priv->ordering_overrides, DATADIR "/" OVERRIDE_FILE_NAME);
	gchar * userfile = g_build_filename(g_get_user_data_dir(), "indicators", "application", OVERRIDE_FILE_NAME, NULL);
	load_override_file(priv->ordering_overrides, userfile);
	g_free(userfile);

	priv->bus_cancel = g_cancellable_new();
	g_bus_get(G_BUS_TYPE_SESSION,
	          priv->bus_cancel,
	          bus_get_cb,
	          self);

	self->priv = priv;

	return;
}

static void
bus_get_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
	GError * error = NULL;
	GDBusConnection * connection = g_bus_get_finish(res, &error);

	if (error != NULL) {
		g_error("OMG! Unable to get a connection to DBus: %s", error->message);
		g_error_free(error);
		return;
	}

	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE_GET_PRIVATE (user_data);

	g_warn_if_fail(priv->bus == NULL);
	priv->bus = connection;

	if (priv->bus_cancel != NULL) {
		g_object_unref(priv->bus_cancel);
		priv->bus_cancel = NULL;
	}

	/* Now register our object on our new connection */
	priv->dbus_registration = g_dbus_connection_register_object(priv->bus,
	                                                            INDICATOR_APPLICATION_DBUS_OBJ,
	                                                            interface_info,
	                                                            &interface_table,
	                                                            user_data,
	                                                            NULL,
	                                                            &error);

	if (error != NULL) {
		g_error("Unable to register the object to DBus: %s", error->message);
		g_error_free(error);
		return;
	}

	return;	
}

/* A method has been called from our dbus inteface.  Figure out what it
   is and dispatch it. */
static void
bus_method_call (GDBusConnection * connection, const gchar * sender,
                 const gchar * path, const gchar * interface,
                 const gchar * method, GVariant * params,
                 GDBusMethodInvocation * invocation, gpointer user_data)
{
	ApplicationServiceAppstore * service = APPLICATION_SERVICE_APPSTORE(user_data);
	GVariant * retval = NULL;

	if (g_strcmp0(method, "GetApplications") == 0) {
		retval = get_applications(service);
	} else {
		g_warning("Calling method '%s' on the indicator service and it's unknown", method);
	}

	g_dbus_method_invocation_return_value(invocation, retval);
	return;
}

static void
application_service_appstore_dispose (GObject *object)
{
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE(object)->priv;

	while (priv->applications != NULL) {
		application_service_appstore_application_remove(APPLICATION_SERVICE_APPSTORE(object),
		                                           ((Application *)priv->applications->data)->dbus_name,
		                                           ((Application *)priv->applications->data)->dbus_object);
	}

	if (priv->approvers != NULL) {
		g_list_foreach(priv->approvers, approver_free, object);
		g_list_free(priv->approvers);
		priv->approvers = NULL;
	}

	if (priv->dbus_registration != 0) {
		g_dbus_connection_unregister_object(priv->bus, priv->dbus_registration);
		/* Don't care if it fails, there's nothing we can do */
		priv->dbus_registration = 0;
	}

	if (priv->bus != NULL) {
		g_object_unref(priv->bus);
		priv->bus = NULL;
	}

	if (priv->bus_cancel != NULL) {
		g_cancellable_cancel(priv->bus_cancel);
		g_object_unref(priv->bus_cancel);
		priv->bus_cancel = NULL;
	}

	G_OBJECT_CLASS (application_service_appstore_parent_class)->dispose (object);
	return;
}

static void
application_service_appstore_finalize (GObject *object)
{
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE(object)->priv;

	if (priv->ordering_overrides != NULL) {
		g_hash_table_destroy(priv->ordering_overrides);
		priv->ordering_overrides = NULL;
	}

	G_OBJECT_CLASS (application_service_appstore_parent_class)->finalize (object);
	return;
}

/* Loads the file and adds the override entries to the table
   of overrides */
static void
load_override_file (GHashTable * hash, const gchar * filename)
{
	g_return_if_fail(hash != NULL);
	g_return_if_fail(filename != NULL);

	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		return;
	}

	g_debug("Loading overrides from: '%s'", filename);

	GError * error = NULL;
	GKeyFile * keyfile = g_key_file_new();
	g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_NONE, &error);

	if (error != NULL) {
		g_warning("Unable to load keyfile '%s' because: %s", filename, error->message);
		g_error_free(error);
		g_key_file_free(keyfile);
		return;
	}

	gchar ** keys = g_key_file_get_keys(keyfile, OVERRIDE_GROUP_NAME, NULL, &error);
	if (error != NULL) {
		g_warning("Unable to get keys from keyfile '%s' because: %s", filename, error->message);
		g_error_free(error);
		g_key_file_free(keyfile);
		return;
	}

	gchar * key = keys[0];
	gint i;

	for (i = 0; (key = keys[i]) != NULL; i++) {
		GError * valerror = NULL;
		gint val = g_key_file_get_integer(keyfile, OVERRIDE_GROUP_NAME, key, &valerror);

		if (valerror != NULL) {
			g_warning("Unable to get key '%s' out of file '%s' because: %s", key, filename, valerror->message);
			g_error_free(valerror);
			continue;
		}
		g_debug("%s: override '%s' with value '%d'", filename, key, val);

		g_hash_table_insert(hash, g_strdup(key), GINT_TO_POINTER(val));
	}
	g_strfreev(keys);
	g_key_file_free(keyfile);

	return;
}

/* Return from getting the properties from the item.  We're looking at those
   and making sure we have everythign that we need.  If we do, then we'll
   move on up to sending this onto the indicator. */
static void
get_all_properties (Application * app)
{
	ApplicationServiceAppstorePrivate * priv = app->appstore->priv;
	GVariant * menu, * id, * category, * status, * icon_name;

	menu = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                        NOTIFICATION_ITEM_PROP_MENU);
	id = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                      NOTIFICATION_ITEM_PROP_ID);
	category = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                            NOTIFICATION_ITEM_PROP_CATEGORY);
	status = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                          NOTIFICATION_ITEM_PROP_STATUS);
	icon_name = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                             NOTIFICATION_ITEM_PROP_ICON_NAME);

	if (menu == NULL || id == NULL || category == NULL || status == NULL ||
	    icon_name == NULL) {
		g_warning("Notification Item on object %s of %s doesn't have enough properties.", app->dbus_object, app->dbus_name);
		if (menu)      g_variant_unref (menu);
		if (id)        g_variant_unref (id);
		if (category)  g_variant_unref (category);
		if (status)    g_variant_unref (status);
		if (icon_name) g_variant_unref (icon_name);
		g_free(app); // Need to do more than this, but it gives the idea of the flow we're going for.
		return;
	}

	app->validated = TRUE;

	app->id = g_variant_dup_string(id, NULL);
	app->category = g_variant_dup_string(category, NULL);
	app->status = string_to_status(g_variant_get_string(status, NULL));
	app->icon = g_variant_dup_string(icon_name, NULL);
	app->menu = g_variant_dup_string(menu, NULL);

	/* Now the optional properties */

	GVariant * aicon_name, * icon_theme_path, * index, * label, * guide;

	aicon_name = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                              NOTIFICATION_ITEM_PROP_AICON_NAME);
	icon_theme_path = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                                   NOTIFICATION_ITEM_PROP_ICON_THEME_PATH);
	index = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                         NOTIFICATION_ITEM_PROP_ORDERING_INDEX);
	label = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                         NOTIFICATION_ITEM_PROP_LABEL);
	guide = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                         NOTIFICATION_ITEM_PROP_LABEL_GUIDE);

	if (aicon_name != NULL) {
		app->aicon = g_variant_dup_string(aicon_name, NULL);
	}

	if (icon_theme_path != NULL) {
		app->icon_theme_path = g_variant_dup_string(icon_theme_path, NULL);
	} else {
		app->icon_theme_path = g_strdup("");
	}

	gpointer ordering_index_over = g_hash_table_lookup(priv->ordering_overrides, app->id);
	if (ordering_index_over == NULL) {
		if (index == NULL || g_variant_get_uint32(index) == 0) {
			app->ordering_index = generate_id(string_to_cat(app->category), app->id);
		} else {
			app->ordering_index = g_variant_get_uint32(index);
		}
	} else {
		app->ordering_index = GPOINTER_TO_UINT(ordering_index_over);
	}
	g_debug("'%s' ordering index is '%X'", app->id, app->ordering_index);

	if (label != NULL) {
		app->label = g_variant_dup_string(label, NULL);
	} else {
		app->label = g_strdup("");
	}

	if (guide != NULL) {
		app->guide = g_variant_dup_string(guide, NULL);
	} else {
		app->guide = g_strdup("");
	}

	priv->applications = g_list_insert_sorted_with_data (priv->applications, app, app_sort_func, NULL);
	g_list_foreach(priv->approvers, check_with_old_approver, app);

	apply_status(app);

	g_variant_unref (menu);
	g_variant_unref (id);
	g_variant_unref (category);
	g_variant_unref (status);
	g_variant_unref (icon_name);
	if (aicon_name)      g_variant_unref (aicon_name);
	if (icon_theme_path) g_variant_unref (icon_theme_path);
	if (index)           g_variant_unref (index);
	if (label)           g_variant_unref (label);
	if (guide)           g_variant_unref (guide);

	return;
}

/* Check the application against an approver */
static void
check_with_old_approver (gpointer papprove, gpointer papp)
{
	/* Funny the parallels, eh? */
	check_with_new_approver(papp, papprove);
	return;
}

/* Simple translation function -- could be optimized */
static AppIndicatorStatus
string_to_status(const gchar * status_string)
{
	GEnumClass * klass = G_ENUM_CLASS(g_type_class_ref(APP_INDICATOR_TYPE_INDICATOR_STATUS));
	g_return_val_if_fail(klass != NULL, APP_INDICATOR_STATUS_PASSIVE);

	AppIndicatorStatus retval = APP_INDICATOR_STATUS_PASSIVE;

	GEnumValue * val = g_enum_get_value_by_nick(klass, status_string);
	if (val == NULL) {
		g_warning("Unrecognized status '%s' assuming passive.", status_string);
	} else {
		retval = (AppIndicatorStatus)val->value;
	}

	g_type_class_unref(klass);

	return retval;
}

/* Simple translation function -- could be optimized */
static AppIndicatorCategory
string_to_cat(const gchar * cat_string)
{
	GEnumClass * klass = G_ENUM_CLASS(g_type_class_ref(APP_INDICATOR_TYPE_INDICATOR_CATEGORY));
	g_return_val_if_fail(klass != NULL, APP_INDICATOR_CATEGORY_OTHER);

	AppIndicatorCategory retval = APP_INDICATOR_CATEGORY_OTHER;

	GEnumValue * val = g_enum_get_value_by_nick(klass, cat_string);
	if (val == NULL) {
		g_warning("Unrecognized status '%s' assuming other.", cat_string);
	} else {
		retval = (AppIndicatorCategory)val->value;
	}

	g_type_class_unref(klass);

	return retval;
}


/* A small helper function to get the position of an application
   in the app list of the applications that are visible. */
static gint 
get_position (Application * app) {
	ApplicationServiceAppstore * appstore = app->appstore;
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	GList * lapp;
	gint count;

	/* Go through the list and try to find ours */
	for (lapp = priv->applications, count = 0; lapp != NULL; lapp = g_list_next(lapp), count++) {
		if (lapp->data == app) {
			break;
		}

		/* If the selected app isn't visible let's not
		   count it's position */
		Application * thisapp = (Application *)(lapp->data);
		if (thisapp->visible_state == VISIBLE_STATE_HIDDEN) {
			count--;
		}
	}

	if (lapp == NULL) {
		g_warning("Unable to find position for app '%s'", app->id);
		return -1;
	}
	
	return count;
}

/* A simple global function for dealing with freeing the information
   in an Application structure */
static void
application_free (Application * app)
{
	if (app == NULL) return;
	g_debug("Application free '%s'", app->id);
	
	/* Handle the case where this could be called by unref'ing one of
	   the proxy objects. */
	if (app->currently_free) return;
	app->currently_free = TRUE;
	
	if (app->dbus_proxy) {
		g_object_unref(app->dbus_proxy);
	}

	if (app->dbus_proxy_cancel != NULL) {
		g_cancellable_cancel(app->dbus_proxy_cancel);
		g_object_unref(app->dbus_proxy_cancel);
		app->dbus_proxy_cancel = NULL;
	}

	if (app->id != NULL) {
		g_free(app->id);
	}
	if (app->category != NULL) {
		g_free(app->category);
	}
	if (app->dbus_name != NULL) {
		g_free(app->dbus_name);
	}
	if (app->dbus_object != NULL) {
		g_free(app->dbus_object);
	}
	if (app->icon != NULL) {
		g_free(app->icon);
	}
	if (app->aicon != NULL) {
		g_free(app->aicon);
	}
	if (app->menu != NULL) {
		g_free(app->menu);
	}
	if (app->icon_theme_path != NULL) {
		g_free(app->icon_theme_path);
	}
	if (app->label != NULL) {
		g_free(app->label);
	}
	if (app->guide != NULL) {
		g_free(app->guide);
	}
	if (app->approved_by != NULL) {
		g_list_free(app->approved_by);
	}

	g_free(app);
	return;
}

/* Gets called when the proxy changes owners, which is usually when it
   drops off of the bus. */
static void
application_owner_changed (GObject * gobject, GParamSpec * pspec,
                           gpointer user_data)
{
	Application * app = (Application *)user_data;
	GDBusProxy * proxy = G_DBUS_PROXY(gobject);

	if (proxy != NULL) { /* else if NULL, assume dead */
		gchar * owner = g_dbus_proxy_get_name_owner(proxy);
		if (owner != NULL) {
			get_all_properties(app); /* Regrab properties for new owner */
			g_free (owner);
			return;
		}
	}

	/* Application died */
	g_debug("Application proxy destroyed '%s'", app->id);

	/* Remove from the panel */
	app->status = APP_INDICATOR_STATUS_PASSIVE;
	apply_status(app);

	/* Remove from the application list */
	app->appstore->priv->applications = g_list_remove(app->appstore->priv->applications, app);

	/* Destroy the data */
	application_free(app);
	return;
}

/* This function takes two Application structure
   pointers and uses their ordering index to compare them. */
static gint
app_sort_func (gconstpointer a, gconstpointer b, gpointer userdata)
{
	Application * appa = (Application *)a;
	Application * appb = (Application *)b;
	return (appb->ordering_index/2) - (appa->ordering_index/2);
}

static void
emit_signal (ApplicationServiceAppstore * appstore, const gchar * name,
             GVariant * variant)
{
	ApplicationServiceAppstorePrivate * priv = appstore->priv;
	GError * error = NULL;

	g_dbus_connection_emit_signal (priv->bus,
		                       NULL,
		                       INDICATOR_APPLICATION_DBUS_OBJ,
		                       INDICATOR_APPLICATION_DBUS_IFACE,
		                       name,
		                       variant,
		                       &error);

	if (error != NULL) {
		g_error("Unable to send %s signal: %s", name, error->message);
		g_error_free(error);
		return;
	}

	return;
}

/* Change the status of the application.  If we're going passive
   it removes it from the panel.  If we're coming online, then
   it add it to the panel.  Otherwise it changes the icon. */
static void
apply_status (Application * app)
{
	ApplicationServiceAppstore * appstore = app->appstore;
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	/* g_debug("Applying status.  Status: %d  Approved by: %d  Approvers: %d  Visible: %d", app->status, g_list_length(app->approved_by), g_list_length(priv->approvers), app->visible_state); */

	visible_state_t goal_state = VISIBLE_STATE_HIDDEN;

	if (app->status != APP_INDICATOR_STATUS_PASSIVE && 
			g_list_length(app->approved_by) >= g_list_length(priv->approvers)) {
		goal_state = VISIBLE_STATE_SHOWN;
	}

	/* Nothing needs to change, we're good */
	if (app->visible_state == goal_state /* ) { */
		&& goal_state == VISIBLE_STATE_HIDDEN) {
		/* TODO: Uhg, this is a little wrong in that we're going to
		   send an icon every time the status changes and the indicator
		   is visible even though it might not be updating.  But, at
		   this point we need a small patch that is harmless.  In the
		   future we need to track which icon is shown and remove the
		   duplicate message. */
		return;
	}

	g_debug("Changing app '%s' state from %s to %s", app->id, STATE2STRING(app->visible_state), STATE2STRING(goal_state));

	/* This means we're going off line */
	if (goal_state == VISIBLE_STATE_HIDDEN) {
		gint position = get_position(app);
		if (position == -1) return;

		emit_signal (appstore, "ApplicationRemoved",
		             g_variant_new ("(i)", position));
	} else {
		/* Figure out which icon we should be using */
		gchar * newicon = app->icon;
		if (app->status == APP_INDICATOR_STATUS_ATTENTION && app->aicon != NULL && app->aicon[0] != '\0') {
			newicon = app->aicon;
		}

		/* Determine whether we're already shown or not */
		if (app->visible_state == VISIBLE_STATE_HIDDEN) {
			/* Put on panel */
			emit_signal (appstore, "ApplicationAdded",
				     g_variant_new ("(sisosss)", newicon,
			                            get_position(app),
			                            app->dbus_name, app->menu,
			                            app->icon_theme_path,
			                            app->label, app->guide));
		} else {
			/* Icon update */
			gint position = get_position(app);
			if (position == -1) return;

			emit_signal (appstore, "ApplicationIconChanged",
				     g_variant_new ("(is)", position, newicon));
		}
	}

	app->visible_state = goal_state;

	return;
}

/* Called when the Notification Item signals that it
   has a new icon. */
static void
new_icon (Application * app, const gchar * newicon)
{
	/* Grab the icon and make sure we have one */
	if (newicon == NULL) {
		g_warning("Bad new icon :(");
		return;
	}

	if (g_strcmp0(newicon, app->icon)) {
		/* If the new icon is actually a new icon */
		if (app->icon != NULL) g_free(app->icon);
		app->icon = g_strdup(newicon);

		if (app->visible_state == VISIBLE_STATE_SHOWN && app->status == APP_INDICATOR_STATUS_ACTIVE) {
			gint position = get_position(app);
			if (position == -1) return;

			emit_signal (app->appstore, "ApplicationIconChanged",
				     g_variant_new ("(is)", position, newicon));
		}
	}

	return;
}

/* Called when the Notification Item signals that it
   has a new attention icon. */
static void
new_aicon (Application * app, const gchar * newicon)
{
	/* Grab the icon and make sure we have one */
	if (newicon == NULL) {
		g_warning("Bad new icon :(");
		return;
	}

	if (g_strcmp0(newicon, app->aicon)) {
		/* If the new icon is actually a new icon */
		if (app->aicon != NULL) g_free(app->aicon);
		app->aicon = g_strdup(newicon);

		if (app->visible_state == VISIBLE_STATE_SHOWN && app->status == APP_INDICATOR_STATUS_ATTENTION) {
			gint position = get_position(app);
			if (position == -1) return;

			emit_signal (app->appstore, "ApplicationIconChanged",
				     g_variant_new ("(is)", position, newicon));
		}
	}

	return;
}

/* Called when the Notification Item signals that it
   has a new status. */
static void
new_status (Application * app, const gchar * status)
{
	app->status = string_to_status(status);
	apply_status(app);

	return;
}

/* Called when the Notification Item signals that it
   has a new icon theme path. */
static void
new_icon_theme_path (Application * app, const gchar * icon_theme_path)
{
	if (g_strcmp0(icon_theme_path, app->icon_theme_path)) {
		/* If the new icon theme path is actually a new icon theme path */
		if (app->icon_theme_path != NULL) g_free(app->icon_theme_path);
		app->icon_theme_path = g_strdup(icon_theme_path);

		if (app->visible_state != VISIBLE_STATE_HIDDEN) {
			gint position = get_position(app);
			if (position == -1) return;

			emit_signal (app->appstore,
			             "ApplicationIconThemePathChanged",
				     g_variant_new ("(is)", position,
			                            app->icon_theme_path));
		}
	}

	return;
}

/* Called when the Notification Item signals that it
   has a new label. */
static void
new_label (Application * app, const gchar * label, const gchar * guide)
{
	gboolean changed = FALSE;

	if (g_strcmp0(app->label, label) != 0) {
		changed = TRUE;
		if (app->label != NULL) {
			g_free(app->label);
			app->label = NULL;
		}
		app->label = g_strdup(label);
	}

	if (g_strcmp0(app->guide, guide) != 0) {
		changed = TRUE;
		if (app->guide != NULL) {
			g_free(app->guide);
			app->guide = NULL;
		}
		app->guide = g_strdup(guide);
	}

	if (changed) {
		gint position = get_position(app);
		if (position == -1) return;

		emit_signal (app->appstore, "ApplicationLabelChanged",
			     g_variant_new ("(iss)", position,
		                            app->label != NULL ? app->label : "",
		                            app->guide != NULL ? app->guide : ""));
	}

	return;
}

/* Adding a new NotificationItem object from DBus in to the
   appstore.  First, we need to get the information on it
   though. */
void
application_service_appstore_application_add (ApplicationServiceAppstore * appstore, const gchar * dbus_name, const gchar * dbus_object)
{
	g_debug("Adding new application: %s:%s", dbus_name, dbus_object);

	/* Make sure we got a sensible request */
	g_return_if_fail(IS_APPLICATION_SERVICE_APPSTORE(appstore));
	g_return_if_fail(dbus_name != NULL && dbus_name[0] != '\0');
	g_return_if_fail(dbus_object != NULL && dbus_object[0] != '\0');
	Application * app = find_application(appstore, dbus_name, dbus_object);

	if (app != NULL) {
		g_warning("Application already exists! Rerequesting properties.");
		get_all_properties(app);
		return;
	}

	/* Build the application entry.  This will be carried
	   along until we're sure we've got everything. */
	app = g_new0(Application, 1);

	app->validated = FALSE;
	app->dbus_name = g_strdup(dbus_name);
	app->dbus_object = g_strdup(dbus_object);
	app->appstore = appstore;
	app->status = APP_INDICATOR_STATUS_PASSIVE;
	app->icon = NULL;
	app->aicon = NULL;
	app->menu = NULL;
	app->icon_theme_path = NULL;
	app->label = NULL;
	app->guide = NULL;
	app->currently_free = FALSE;
	app->ordering_index = 0;
	app->approved_by = NULL;
	app->visible_state = VISIBLE_STATE_HIDDEN;

	/* Get the DBus proxy for the NotificationItem interface */
	app->dbus_proxy_cancel = g_cancellable_new();
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
			         G_DBUS_PROXY_FLAGS_NONE,
			         NULL,
	                         app->dbus_name,
	                         app->dbus_object,
	                         NOTIFICATION_ITEM_DBUS_IFACE,
			         app->dbus_proxy_cancel,
			         dbus_proxy_cb,
		                 app);

	/* We're returning, nothing is yet added until the properties
	   come back and give us more info. */
	return;
}

/* Callback from trying to create the proxy for the app. */
static void
dbus_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
	GError * error = NULL;

	Application * app = (Application *)user_data;
	g_return_if_fail(app != NULL);

	GDBusProxy * proxy = g_dbus_proxy_new_for_bus_finish(res, &error);

	if (app->dbus_proxy_cancel != NULL) {
		g_object_unref(app->dbus_proxy_cancel);
		app->dbus_proxy_cancel = NULL;
	}

	if (error != NULL) {
		g_error("Could not grab DBus proxy for %s: %s", app->dbus_name, error->message);
		g_error_free(error);
		return;
	}

	/* Okay, we're good to grab the proxy at this point, we're
	sure that it's ours. */
	app->dbus_proxy = proxy;

	/* We've got it, let's watch it for destruction */
	g_signal_connect(proxy, "notify::g-name-owner",
	                 G_CALLBACK(application_owner_changed), app);
	g_signal_connect(proxy, "g-signal", G_CALLBACK(app_receive_signal), app);

	get_all_properties(app);

	return;
}

/* Receives all signals from the service, routed to the appropriate functions */
static void
app_receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name,
                    GVariant * parameters, gpointer user_data)
{
	Application * app = (Application *)user_data;

	if (!app->validated) return;

	if (g_strcmp0(signal_name, NOTIFICATION_ITEM_SIG_NEW_ICON) == 0) {
		/* icon name isn't provided by signal, so look it up */
		GVariant * icon_name = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                                                NOTIFICATION_ITEM_PROP_ICON_NAME);
		if (icon_name) {
			new_icon(app, g_variant_get_string(icon_name, NULL));
			g_variant_unref(icon_name);
		}
	}
	else if (g_strcmp0(signal_name, NOTIFICATION_ITEM_SIG_NEW_AICON) == 0) {
		/* aicon name isn't provided by signal, so look it up */
		GVariant * aicon_name = g_dbus_proxy_get_cached_property(app->dbus_proxy,
	                                                                 NOTIFICATION_ITEM_PROP_AICON_NAME);
		if (aicon_name) {
			new_aicon(app, g_variant_get_string(aicon_name, NULL));
			g_variant_unref(aicon_name);
		}
	}
	else if (g_strcmp0(signal_name, NOTIFICATION_ITEM_SIG_NEW_STATUS) == 0) {
		const gchar * status;
		g_variant_get(parameters, "(&s)", &status);
		new_status(app, status);
	}
	else if (g_strcmp0(signal_name, NOTIFICATION_ITEM_SIG_NEW_ICON_THEME_PATH) == 0) {
		const gchar * icon_theme_path;
		g_variant_get(parameters, "(&s)", &icon_theme_path);
		new_icon_theme_path(app, icon_theme_path);
	}
	else if (g_strcmp0(signal_name, NOTIFICATION_ITEM_SIG_NEW_LABEL) == 0) {
		const gchar * label, * guide;
		g_variant_get(parameters, "(&s&s)", &label, &guide);
		new_label(app, label, guide);
	}

	return;
}

/* Looks for an application in the list of applications */
static Application *
find_application (ApplicationServiceAppstore * appstore, const gchar * address, const gchar * object)
{
	ApplicationServiceAppstorePrivate * priv = appstore->priv;
	GList * listpntr;

	for (listpntr = priv->applications; listpntr != NULL; listpntr = g_list_next(listpntr)) {
		Application * app = (Application *)listpntr->data;

		if (!g_strcmp0(app->dbus_name, address) && !g_strcmp0(app->dbus_object, object)) {
			return app;
		}
	}

	return NULL;
}

/* Removes an application.  Currently only works for the apps
   that are shown. */
void
application_service_appstore_application_remove (ApplicationServiceAppstore * appstore, const gchar * dbus_name, const gchar * dbus_object)
{
	g_return_if_fail(IS_APPLICATION_SERVICE_APPSTORE(appstore));
	g_return_if_fail(dbus_name != NULL && dbus_name[0] != '\0');
	g_return_if_fail(dbus_object != NULL && dbus_object[0] != '\0');

	Application * app = find_application(appstore, dbus_name, dbus_object);
	if (app != NULL) {
		application_owner_changed(NULL, NULL, app);
	} else {
		g_warning("Unable to find application %s:%s", dbus_name, dbus_object);
	}

	return;
}

gchar**
application_service_appstore_application_get_list (ApplicationServiceAppstore * appstore)
{
	ApplicationServiceAppstorePrivate * priv = appstore->priv;
	gchar ** out;
	gchar ** outpntr;
	GList * listpntr;

	out = g_new(gchar*, g_list_length(priv->applications) + 1);

	for (listpntr = priv->applications, outpntr = out; listpntr != NULL; listpntr = g_list_next(listpntr), ++outpntr) {
		Application * app = (Application *)listpntr->data;
		*outpntr = g_strdup_printf("%s%s", app->dbus_name, app->dbus_object);
	}
	*outpntr = 0;
	return out;
}

/* Creates a basic appstore object and attaches the
   LRU file object to it. */
ApplicationServiceAppstore *
application_service_appstore_new (void)
{
	ApplicationServiceAppstore * appstore = APPLICATION_SERVICE_APPSTORE(g_object_new(APPLICATION_SERVICE_APPSTORE_TYPE, NULL));
	return appstore;
}

/* DBus Interface */
static GVariant *
get_applications (ApplicationServiceAppstore * appstore)
{
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	GVariantBuilder * builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	GList * listpntr;
	gint position = 0;

	for (listpntr = priv->applications; listpntr != NULL; listpntr = g_list_next(listpntr)) {
		Application * app = (Application *)listpntr->data;
		if (app->visible_state == VISIBLE_STATE_HIDDEN) {
			continue;
		}

		g_variant_builder_add (builder, "(sisosss)", app->icon,
		                       position++, app->dbus_name, app->menu,
		                       app->icon_theme_path, app->label,
		                       app->guide);
	}

	return g_variant_new("(a(sisosss))", builder);
}

/* Removes and approver from our list of approvers and
   then sees if that changes our status.  Most likely this
   could make us visible if this approver rejected us. */
static void
remove_approver (gpointer papp, gpointer pproxy)
{
	Application * app = (Application *)papp;
	app->approved_by = g_list_remove(app->approved_by, pproxy);
	apply_status(app);
	return;
}

/* Frees the data associated with an approver */
static void
approver_free (gpointer papprover, gpointer user_data)
{
	Approver * approver = (Approver *)papprover;
	g_return_if_fail(approver != NULL);

	ApplicationServiceAppstore * appstore = APPLICATION_SERVICE_APPSTORE(user_data);
	g_list_foreach(appstore->priv->applications, remove_approver, approver->proxy);
	
	if (approver->proxy != NULL) {
		g_object_unref(approver->proxy);
		approver->proxy = NULL;
	}

	if (approver->proxy_cancel != NULL) {
		g_cancellable_cancel(approver->proxy_cancel);
		g_object_unref(approver->proxy_cancel);
		approver->proxy_cancel = NULL;
	}

	g_free(approver);
	return;
}

/* What did the approver tell us? */
static void
approver_request_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy * proxy = G_DBUS_PROXY(object);
	Application * app = (Application *)user_data;
	GError * error = NULL;
	gboolean approved = TRUE; /* default to approved */
	GVariant * result;

	result = g_dbus_proxy_call_finish(proxy, res, &error);

	if (error == NULL) {
		g_variant_get(result, "(b)", &approved);
		g_debug("Approver responded: %s", approved ? "approve" : "rejected");
	}
	else {
		g_debug("Approver responded error: %s", error->message);
	}

	if (approved) {
		app->approved_by = g_list_prepend(app->approved_by, proxy);
	} else {
		app->approved_by = g_list_remove(app->approved_by, proxy);
	}

	apply_status(app);
	return;
}

/* Run the applications through the new approver */
static void
check_with_new_approver (gpointer papp, gpointer papprove)
{
	Application * app = (Application *)papp;
	Approver * approver = (Approver *)papprove;

	g_dbus_proxy_call(approver->proxy, "ApproveItem",
	                  g_variant_new("(ssuso)", app->id, app->category,
	                                0, app->dbus_name, app->dbus_object),
	                  G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                  approver_request_cb, app);

	return;
}

/* Tracks when a proxy gets destroyed so that we know that the
   approver has dropped off the bus. */
static void
approver_owner_changed (GObject * gobject, GParamSpec * pspec,
                        gpointer user_data)
{
	Approver * approver = (Approver *)user_data;
	ApplicationServiceAppstore * appstore = approver->appstore;
	GDBusProxy * proxy = G_DBUS_PROXY(gobject);

	gchar * owner = g_dbus_proxy_get_name_owner(proxy);
	if (owner != NULL) {
		/* Reapprove everything with new owner */
		g_list_foreach(appstore->priv->applications, check_with_new_approver, approver);
		g_free (owner);
		return;
	}

	/* Approver died */
	appstore->priv->approvers = g_list_remove(appstore->priv->approvers, approver);
	approver_free(approver, appstore);

	return;
}

/* A signal when an approver changes the why that it thinks about
   a particular indicator. */
void
approver_revise_judgement (Approver * approver, gboolean new_status, const gchar * address, const gchar * path)
{
	g_return_if_fail(address != NULL && address[0] != '\0');
	g_return_if_fail(path != NULL && path[0] != '\0');

	Application * app = find_application(approver->appstore, address, path);

	if (app == NULL) {
		g_warning("Unable to update approver status of application (%s:%s) as it was not found", address, path);
		return;
	}

	if (new_status) {
		app->approved_by = g_list_prepend(app->approved_by, approver->proxy);
	} else {
		app->approved_by = g_list_remove(app->approved_by, approver->proxy);
	}
	apply_status(app);

	return;
}

/* Adds a new approver to the app store */
void
application_service_appstore_approver_add (ApplicationServiceAppstore * appstore, const gchar * dbus_name, const gchar * dbus_object)
{
	g_return_if_fail(IS_APPLICATION_SERVICE_APPSTORE(appstore));
	g_return_if_fail(dbus_name != NULL);
	g_return_if_fail(dbus_object != NULL);
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE_GET_PRIVATE (appstore);

	Approver * approver = g_new0(Approver, 1);
	approver->appstore = appstore;
	approver->proxy_cancel = NULL;
	approver->proxy = NULL;

	approver->proxy_cancel = g_cancellable_new();
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
			         G_DBUS_PROXY_FLAGS_NONE,
			         NULL,
	                         dbus_name,
	                         dbus_object,
	                         NOTIFICATION_APPROVER_DBUS_IFACE,
			         approver->proxy_cancel,
			         approver_proxy_cb,
		                 approver);

	priv->approvers = g_list_prepend(priv->approvers, approver);

	return;
}

/* Callback from trying to create the proxy for the approver. */
static void
approver_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
	GError * error = NULL;

	Approver * approver = (Approver *)user_data;
	g_return_if_fail(approver != NULL);

	GDBusProxy * proxy = g_dbus_proxy_new_for_bus_finish(res, &error);
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE_GET_PRIVATE (approver->appstore);

	if (approver->proxy_cancel != NULL) {
		g_object_unref(approver->proxy_cancel);
		approver->proxy_cancel = NULL;
	}

	if (error != NULL) {
		g_error("Could not grab DBus proxy for approver: %s", error->message);
		g_error_free(error);
		return;
	}

	/* Okay, we're good to grab the proxy at this point, we're
	sure that it's ours. */
	approver->proxy = proxy;

	/* We've got it, let's watch it for destruction */
	g_signal_connect(proxy, "notify::g-name-owner",
	                 G_CALLBACK(approver_owner_changed), approver);
	g_signal_connect(proxy, "g-signal", G_CALLBACK(approver_receive_signal),
	                 approver);

	g_list_foreach(priv->applications, check_with_new_approver, approver);

	return;
}

/* Receives all signals from the service, routed to the appropriate functions */
static void
approver_receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name,
                         GVariant * parameters, gpointer user_data)
{
	Approver * approver = (Approver *)user_data;

	if (g_strcmp0(signal_name, "ReviseJudgement") == 0) {
		gboolean approved;
		const gchar * address;
		const gchar * path;
		g_variant_get(parameters, "(b&s&o)", &approved, &address, &path);
		approver_revise_judgement(approver, approved, address, path);
	}

	return;
}

