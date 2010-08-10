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

#include <dbus/dbus-glib.h>
#include "app-indicator.h"
#include "app-indicator-enum-types.h"
#include "application-service-appstore.h"
#include "application-service-marshal.h"
#include "dbus-properties-client.h"
#include "dbus-shared.h"
#include "notification-approver-client.h"
#include "generate-id.h"

/* DBus Prototypes */
static gboolean _application_service_server_get_applications (ApplicationServiceAppstore * appstore, GPtrArray ** apps, GError ** error);

#include "application-service-server.h"

#define NOTIFICATION_ITEM_PROP_ID                    "Id"
#define NOTIFICATION_ITEM_PROP_CATEGORY              "Category"
#define NOTIFICATION_ITEM_PROP_STATUS                "Status"
#define NOTIFICATION_ITEM_PROP_ICON_NAME             "IconName"
#define NOTIFICATION_ITEM_PROP_AICON_NAME            "AttentionIconName"
#define NOTIFICATION_ITEM_PROP_ICON_THEME_PATH       "IconThemePath"
#define NOTIFICATION_ITEM_PROP_MENU                  "Menu"
#define NOTIFICATION_ITEM_PROP_LABEL                 "Label"
#define NOTIFICATION_ITEM_PROP_LABEL_GUIDE            "LabelGuide"
#define NOTIFICATION_ITEM_PROP_ORDERING_INDEX        "OrderingIndex"

#define NOTIFICATION_ITEM_SIG_NEW_ICON               "NewIcon"
#define NOTIFICATION_ITEM_SIG_NEW_AICON              "NewAttentionIcon"
#define NOTIFICATION_ITEM_SIG_NEW_STATUS             "NewStatus"
#define NOTIFICATION_ITEM_SIG_NEW_LABEL              "NewLabel"
#define NOTIFICATION_ITEM_SIG_NEW_ICON_THEME_PATH    "NewIconThemePath"

#define OVERRIDE_GROUP_NAME                          "Ordering Index Overrides"
#define OVERRIDE_FILE_NAME                           "ordering-override.keyfile"

/* Private Stuff */
struct _ApplicationServiceAppstorePrivate {
	DBusGConnection * bus;
	GList * applications;
	GList * approvers;
	GHashTable * ordering_overrides;
};

typedef struct _Approver Approver;
struct _Approver {
	DBusGProxy * proxy;
};

typedef struct _Application Application;
struct _Application {
	gchar * id;
	gchar * category;
	gchar * dbus_name;
	gchar * dbus_object;
	ApplicationServiceAppstore * appstore; /* not ref'd */
	DBusGProxy * dbus_proxy;
	DBusGProxy * prop_proxy;
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
};

#define APPLICATION_SERVICE_APPSTORE_GET_PRIVATE(o) \
			(G_TYPE_INSTANCE_GET_PRIVATE ((o), APPLICATION_SERVICE_APPSTORE_TYPE, ApplicationServiceAppstorePrivate))

/* Signals Stuff */
enum {
	APPLICATION_ADDED,
	APPLICATION_REMOVED,
	APPLICATION_ICON_CHANGED,
	APPLICATION_LABEL_CHANGED,
	APPLICATION_ICON_THEME_PATH_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* GObject stuff */
static void application_service_appstore_class_init (ApplicationServiceAppstoreClass *klass);
static void application_service_appstore_init       (ApplicationServiceAppstore *self);
static void application_service_appstore_dispose    (GObject *object);
static void application_service_appstore_finalize   (GObject *object);
static void load_override_file (GHashTable * hash, const gchar * filename);
static AppIndicatorStatus string_to_status(const gchar * status_string);
static void apply_status (Application * app, AppIndicatorStatus status);
static void approver_free (gpointer papprover, gpointer user_data);
static void check_with_new_approver (gpointer papp, gpointer papprove);
static void check_with_old_approver (gpointer papprove, gpointer papp);

G_DEFINE_TYPE (ApplicationServiceAppstore, application_service_appstore, G_TYPE_OBJECT);

static void
application_service_appstore_class_init (ApplicationServiceAppstoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (ApplicationServiceAppstorePrivate));

	object_class->dispose = application_service_appstore_dispose;
	object_class->finalize = application_service_appstore_finalize;

	signals[APPLICATION_ADDED] = g_signal_new ("application-added",
	                                           G_TYPE_FROM_CLASS(klass),
	                                           G_SIGNAL_RUN_LAST,
	                                           G_STRUCT_OFFSET (ApplicationServiceAppstoreClass, application_added),
	                                           NULL, NULL,
	                                           _application_service_marshal_VOID__STRING_INT_STRING_STRING_STRING_STRING_STRING,
	                                           G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_NONE);
	signals[APPLICATION_REMOVED] = g_signal_new ("application-removed",
	                                           G_TYPE_FROM_CLASS(klass),
	                                           G_SIGNAL_RUN_LAST,
	                                           G_STRUCT_OFFSET (ApplicationServiceAppstoreClass, application_removed),
	                                           NULL, NULL,
	                                           g_cclosure_marshal_VOID__INT,
	                                           G_TYPE_NONE, 1, G_TYPE_INT, G_TYPE_NONE);
	signals[APPLICATION_ICON_CHANGED] = g_signal_new ("application-icon-changed",
	                                           G_TYPE_FROM_CLASS(klass),
	                                           G_SIGNAL_RUN_LAST,
	                                           G_STRUCT_OFFSET (ApplicationServiceAppstoreClass, application_icon_changed),
	                                           NULL, NULL,
	                                           _application_service_marshal_VOID__INT_STRING,
	                                           G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING, G_TYPE_NONE);
	signals[APPLICATION_ICON_THEME_PATH_CHANGED] = g_signal_new ("application-icon-theme-path-changed",
	                                           G_TYPE_FROM_CLASS(klass),
	                                           G_SIGNAL_RUN_LAST,
	                                           G_STRUCT_OFFSET (ApplicationServiceAppstoreClass, application_icon_theme_path_changed),
	                                           NULL, NULL,
	                                           _application_service_marshal_VOID__INT_STRING,
	                                           G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING, G_TYPE_NONE);
	signals[APPLICATION_LABEL_CHANGED] = g_signal_new ("application-label-changed",
	                                           G_TYPE_FROM_CLASS(klass),
	                                           G_SIGNAL_RUN_LAST,
	                                           G_STRUCT_OFFSET (ApplicationServiceAppstoreClass, application_label_changed),
	                                           NULL, NULL,
	                                           _application_service_marshal_VOID__INT_STRING_STRING,
	                                           G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_NONE);

	dbus_g_object_register_marshaller(_application_service_marshal_VOID__STRING_STRING,
	                                  G_TYPE_NONE,
	                                  G_TYPE_STRING,
	                                  G_TYPE_STRING,
	                                  G_TYPE_INVALID);

	dbus_g_object_type_install_info(APPLICATION_SERVICE_APPSTORE_TYPE,
	                                &dbus_glib__application_service_server_object_info);

	return;
}

static void
application_service_appstore_init (ApplicationServiceAppstore *self)
{
    
	ApplicationServiceAppstorePrivate * priv = APPLICATION_SERVICE_APPSTORE_GET_PRIVATE (self);

	priv->applications = NULL;
	priv->approvers = NULL;

	priv->ordering_overrides = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	load_override_file(priv->ordering_overrides, DATADIR "/" OVERRIDE_FILE_NAME);
	gchar * userfile = g_build_filename(g_get_user_data_dir(), "indicators", "application", OVERRIDE_FILE_NAME, NULL);
	load_override_file(priv->ordering_overrides, userfile);
	g_free(userfile);
	
	GError * error = NULL;
	priv->bus = dbus_g_bus_get(DBUS_BUS_STARTER, &error);
	if (error != NULL) {
		g_error("Unable to get session bus: %s", error->message);
		g_error_free(error);
		return;
	}

	dbus_g_connection_register_g_object(priv->bus,
	                                    INDICATOR_APPLICATION_DBUS_OBJ,
	                                    G_OBJECT(self));
	                                    
    self->priv = priv;

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
		g_list_foreach(priv->approvers, approver_free, NULL);
		g_list_free(priv->approvers);
		priv->approvers = NULL;
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

	gchar ** keys = g_key_file_get_keys(keyfile, OVERRIDE_GROUP_NAME, NULL, NULL);
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
	g_key_file_free(keyfile);

	return;
}

/* Return from getting the properties from the item.  We're looking at those
   and making sure we have everythign that we need.  If we do, then we'll
   move on up to sending this onto the indicator. */
static void
get_all_properties_cb (DBusGProxy * proxy, GHashTable * properties, GError * error, gpointer data)
{
	if (error != NULL) {
		g_warning("Unable to get properties: %s", error->message);
		return;
	}

	Application * app = (Application *)data;

	if (g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_MENU) == NULL ||
			g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ID) == NULL ||
			g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_CATEGORY) == NULL ||
			g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_STATUS) == NULL ||
			g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ICON_NAME) == NULL) {
		g_warning("Notification Item on object %s of %s doesn't have enough properties.", app->dbus_object, app->dbus_name);
		g_free(app); // Need to do more than this, but it gives the idea of the flow we're going for.
		return;
	}

	app->validated = TRUE;

	app->id = g_value_dup_string(g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ID));
	app->category = g_value_dup_string(g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_CATEGORY));
	ApplicationServiceAppstorePrivate * priv = app->appstore->priv;

	app->icon = g_value_dup_string(g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ICON_NAME));

	GValue * menuval = (GValue *)g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_MENU);
	if (G_VALUE_TYPE(menuval) == G_TYPE_STRING) {
		/* This is here to support an older version where we
		   were using strings instea of object paths. */
		app->menu = g_value_dup_string(menuval);
	} else {
		app->menu = g_strdup((gchar *)g_value_get_boxed(menuval));
	}

	if (g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_AICON_NAME) != NULL) {
		app->aicon = g_value_dup_string(g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_AICON_NAME));
	}

	gpointer icon_theme_path_data = g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ICON_THEME_PATH);
	if (icon_theme_path_data != NULL) {
		app->icon_theme_path = g_value_dup_string((GValue *)icon_theme_path_data);
	} else {
		app->icon_theme_path = g_strdup("");
	}

	gpointer ordering_index_data = g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_ORDERING_INDEX);
	if (ordering_index_data == NULL || g_value_get_uint(ordering_index_data) == 0) {
		app->ordering_index = generate_id(string_to_status(app->category), app->id);
	} else {
		app->ordering_index = g_value_get_uint(ordering_index_data);
	}

	gpointer label_data = g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_LABEL);
	if (label_data != NULL) {
		app->label = g_value_dup_string((GValue *)label_data);
	} else {
		app->label = g_strdup("");
	}

	gpointer guide_data = g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_LABEL_GUIDE);
	if (guide_data != NULL) {
		app->guide = g_value_dup_string((GValue *)guide_data);
	} else {
		app->guide = g_strdup("");
	}

	/* TODO: Calling approvers, but we're ignoring the results.  So, eh. */
	g_list_foreach(priv->approvers, check_with_old_approver, app);

	apply_status(app, string_to_status(g_value_get_string(g_hash_table_lookup(properties, NOTIFICATION_ITEM_PROP_STATUS))));

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

/* A small helper function to get the position of an application
   in the app list. */
static gint 
get_position (Application * app) {
	ApplicationServiceAppstore * appstore = app->appstore;
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	GList * applistitem = g_list_find(priv->applications, app);
	if (applistitem == NULL) {
		return -1;
	}

	return g_list_position(priv->applications, applistitem);
}

/* A simple global function for dealing with freeing the information
   in an Application structure */
static void
application_free (Application * app)
{
	if (app == NULL) return;
	
	/* Handle the case where this could be called by unref'ing one of
	   the proxy objects. */
	if (app->currently_free) return;
	app->currently_free = TRUE;
	
	if (app->dbus_proxy) {
		g_object_unref(app->dbus_proxy);
	}
	if (app->prop_proxy) {
		g_object_unref(app->prop_proxy);
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

	g_free(app);
	return;
}

/* Gets called when the proxy is destroyed, which is usually when it
   drops off of the bus. */
static void
application_removed_cb (DBusGProxy * proxy, gpointer userdata)
{
	Application * app = (Application *)userdata;

	/* Remove from the panel */
	apply_status(app, APP_INDICATOR_STATUS_PASSIVE);

	/* Destroy the data */
	application_free(app);
	return;
}

static gboolean
can_add_application (GList *applications, Application *app)
{
  if (applications)
    {
      GList *l = NULL;

      for (l = applications; l != NULL; l = g_list_next (l))
        {
          Application *tmp_app = (Application *)l->data;

          if (g_strcmp0 (tmp_app->dbus_name, app->dbus_name) == 0 &&
              g_strcmp0 (tmp_app->dbus_object, app->dbus_object) == 0)
            {
              return FALSE;
            }
        }
    }

  return TRUE;
}

/* This function takes two Application structure
   pointers and uses their ordering index to compare them. */
static gint
app_sort_func (gconstpointer a, gconstpointer b, gpointer userdata)
{
	Application * appa = (Application *)a;
	Application * appb = (Application *)b;
	return appa->ordering_index - appb->ordering_index;
}

/* Change the status of the application.  If we're going passive
   it removes it from the panel.  If we're coming online, then
   it add it to the panel.  Otherwise it changes the icon. */
static void
apply_status (Application * app, AppIndicatorStatus status)
{
	if (app->status == status) {
		return;
	}
	g_debug("Changing app status to: %d", status);

	ApplicationServiceAppstore * appstore = app->appstore;
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	/* This means we're going off line */
	if (status == APP_INDICATOR_STATUS_PASSIVE) {
		gint position = get_position(app);
		if (position == -1) return;

		g_signal_emit(G_OBJECT(appstore),
					  signals[APPLICATION_REMOVED], 0, 
					  position, TRUE);
                priv->applications = g_list_remove(priv->applications, app);
	} else {
		/* Figure out which icon we should be using */
		gchar * newicon = app->icon;
		if (status == APP_INDICATOR_STATUS_ATTENTION && app->aicon != NULL && app->aicon[0] != '\0') {
			newicon = app->aicon;
		}

		/* Determine whether we're already shown or not */
		if (app->status == APP_INDICATOR_STATUS_PASSIVE) {
                        if (can_add_application (priv->applications, app)) {
                                /* Put on panel */
                                priv->applications = g_list_insert_sorted_with_data (priv->applications, app, app_sort_func, NULL);

                                g_signal_emit(G_OBJECT(app->appstore),
                                              signals[APPLICATION_ADDED], 0,
                                              newicon,
                                              g_list_index(priv->applications, app), /* Position */
                                              app->dbus_name,
                                              app->menu,
                                              app->icon_theme_path,
                                              app->label,
                                              app->guide,
                                              TRUE);
                        }
		} else {
			/* Icon update */
			gint position = get_position(app);
			if (position == -1) return;

			g_signal_emit(G_OBJECT(appstore),
			              signals[APPLICATION_ICON_CHANGED], 0, 
			              position, newicon, TRUE);
		}
	}

	app->status = status;

	return;
}

/* Gets the data back on an updated icon signal.  Hopefully
   a new fun icon. */
static void
new_icon_cb (DBusGProxy * proxy, GValue value, GError * error, gpointer userdata)
{
	/* Check for errors */
	if (error != NULL) {
		g_warning("Unable to get updated icon name: %s", error->message);
		return;
	}

	/* Grab the icon and make sure we have one */
	const gchar * newicon = g_value_get_string(&value);
	if (newicon == NULL) {
		g_warning("Bad new icon :(");
		return;
	}

	Application * app = (Application *) userdata;

	if (g_strcmp0(newicon, app->icon)) {
		/* If the new icon is actually a new icon */
		if (app->icon != NULL) g_free(app->icon);
		app->icon = g_strdup(newicon);

		if (app->status == APP_INDICATOR_STATUS_ACTIVE) {
			gint position = get_position(app);
			if (position == -1) return;

			g_signal_emit(G_OBJECT(app->appstore),
			              signals[APPLICATION_ICON_CHANGED], 0, 
			              position, newicon, TRUE);
		}
	}

	return;
}

/* Gets the data back on an updated aicon signal.  Hopefully
   a new fun icon. */
static void
new_aicon_cb (DBusGProxy * proxy, GValue value, GError * error, gpointer userdata)
{
	/* Check for errors */
	if (error != NULL) {
		g_warning("Unable to get updated icon name: %s", error->message);
		return;
	}

	/* Grab the icon and make sure we have one */
	const gchar * newicon = g_value_get_string(&value);
	if (newicon == NULL) {
		g_warning("Bad new icon :(");
		return;
	}

	Application * app = (Application *) userdata;

	if (g_strcmp0(newicon, app->aicon)) {
		/* If the new icon is actually a new icon */
		if (app->aicon != NULL) g_free(app->aicon);
		app->aicon = g_strdup(newicon);

		if (app->status == APP_INDICATOR_STATUS_ATTENTION) {
			gint position = get_position(app);
			if (position == -1) return;

			g_signal_emit(G_OBJECT(app->appstore),
			              signals[APPLICATION_ICON_CHANGED], 0, 
			              position, newicon, TRUE);
		}
	}

	return;
}

/* Called when the Notification Item signals that it
   has a new icon. */
static void
new_icon (DBusGProxy * proxy, gpointer data)
{
	Application * app = (Application *)data;
	if (!app->validated) return;

	org_freedesktop_DBus_Properties_get_async(app->prop_proxy,
	                                          NOTIFICATION_ITEM_DBUS_IFACE,
	                                          NOTIFICATION_ITEM_PROP_ICON_NAME,
	                                          new_icon_cb,
	                                          app);
	return;
}

/* Called when the Notification Item signals that it
   has a new attention icon. */
static void
new_aicon (DBusGProxy * proxy, gpointer data)
{
	Application * app = (Application *)data;
	if (!app->validated) return;

	org_freedesktop_DBus_Properties_get_async(app->prop_proxy,
	                                          NOTIFICATION_ITEM_DBUS_IFACE,
	                                          NOTIFICATION_ITEM_PROP_AICON_NAME,
	                                          new_aicon_cb,
	                                          app);

	return;
}

/* Called when the Notification Item signals that it
   has a new status. */
static void
new_status (DBusGProxy * proxy, const gchar * status, gpointer data)
{
	Application * app = (Application *)data;
	if (!app->validated) return;

	apply_status(app, string_to_status(status));

	return;
}

/* Called when the Notification Item signals that it
   has a new icon theme path. */
static void
new_icon_theme_path (DBusGProxy * proxy, const gchar * icon_theme_path, gpointer data)
{
	Application * app = (Application *)data;
	if (!app->validated) return;

	if (g_strcmp0(icon_theme_path, app->icon_theme_path)) {
		/* If the new icon theme path is actually a new icon theme path */
		if (app->icon_theme_path != NULL) g_free(app->icon_theme_path);
		app->icon_theme_path = g_strdup(icon_theme_path);

		if (app->status == APP_INDICATOR_STATUS_ACTIVE) {
			gint position = get_position(app);
			if (position == -1) return;

			g_signal_emit(G_OBJECT(app->appstore),
			              signals[APPLICATION_ICON_THEME_PATH_CHANGED], 0, 
			              position, app->icon_theme_path, TRUE);
		}
	}

	return;
}

/* Called when the Notification Item signals that it
   has a new label. */
static void
new_label (DBusGProxy * proxy, const gchar * label, const gchar * guide, gpointer data)
{
	Application * app = (Application *)data;
	if (!app->validated) return;

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

		g_signal_emit(app->appstore, signals[APPLICATION_LABEL_CHANGED], 0,
					  position,
		              app->label != NULL ? app->label : "", 
		              app->guide != NULL ? app->guide : "",
		              TRUE);
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
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	/* Build the application entry.  This will be carried
	   along until we're sure we've got everything. */
	Application * app = g_new0(Application, 1);

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

	/* Get the DBus proxy for the NotificationItem interface */
	GError * error = NULL;
	app->dbus_proxy = dbus_g_proxy_new_for_name_owner(priv->bus,
	                                                  app->dbus_name,
	                                                  app->dbus_object,
	                                                  NOTIFICATION_ITEM_DBUS_IFACE,
	                                                  &error);

	if (error != NULL) {
		g_warning("Unable to get notification item proxy for object '%s' on host '%s': %s", dbus_object, dbus_name, error->message);
		g_error_free(error);
		g_free(app);
		return;
	}
	
	/* We've got it, let's watch it for destruction */
	g_signal_connect(G_OBJECT(app->dbus_proxy), "destroy", G_CALLBACK(application_removed_cb), app);

	/* Grab the property proxy interface */
	app->prop_proxy = dbus_g_proxy_new_for_name_owner(priv->bus,
	                                                  app->dbus_name,
	                                                  app->dbus_object,
	                                                  DBUS_INTERFACE_PROPERTIES,
	                                                  &error);

	if (error != NULL) {
		g_warning("Unable to get property proxy for object '%s' on host '%s': %s", dbus_object, dbus_name, error->message);
		g_error_free(error);
		g_object_unref(app->dbus_proxy);
		g_free(app);
		return;
	}

	/* Connect to signals */
	dbus_g_proxy_add_signal(app->dbus_proxy,
	                        NOTIFICATION_ITEM_SIG_NEW_ICON,
	                        G_TYPE_INVALID);
	dbus_g_proxy_add_signal(app->dbus_proxy,
	                        NOTIFICATION_ITEM_SIG_NEW_AICON,
	                        G_TYPE_INVALID);
	dbus_g_proxy_add_signal(app->dbus_proxy,
	                        NOTIFICATION_ITEM_SIG_NEW_STATUS,
	                        G_TYPE_STRING,
	                        G_TYPE_INVALID);
    dbus_g_proxy_add_signal(app->dbus_proxy,
	                        NOTIFICATION_ITEM_SIG_NEW_ICON_THEME_PATH,
	                        G_TYPE_STRING,
	                        G_TYPE_INVALID);
	dbus_g_proxy_add_signal(app->dbus_proxy,
	                        NOTIFICATION_ITEM_SIG_NEW_LABEL,
	                        G_TYPE_STRING,
	                        G_TYPE_STRING,
	                        G_TYPE_INVALID);

	dbus_g_proxy_connect_signal(app->dbus_proxy,
	                            NOTIFICATION_ITEM_SIG_NEW_ICON,
	                            G_CALLBACK(new_icon),
	                            app,
	                            NULL);
	dbus_g_proxy_connect_signal(app->dbus_proxy,
	                            NOTIFICATION_ITEM_SIG_NEW_AICON,
	                            G_CALLBACK(new_aicon),
	                            app,
	                            NULL);
	dbus_g_proxy_connect_signal(app->dbus_proxy,
	                            NOTIFICATION_ITEM_SIG_NEW_STATUS,
	                            G_CALLBACK(new_status),
	                            app,
	                            NULL);
    dbus_g_proxy_connect_signal(app->dbus_proxy,
	                            NOTIFICATION_ITEM_SIG_NEW_ICON_THEME_PATH,
	                            G_CALLBACK(new_icon_theme_path),
	                            app,
	                            NULL);
	dbus_g_proxy_connect_signal(app->dbus_proxy,
	                            NOTIFICATION_ITEM_SIG_NEW_LABEL,
	                            G_CALLBACK(new_label),
	                            app,
	                            NULL);

	/* Get all the propertiees */
	org_freedesktop_DBus_Properties_get_all_async(app->prop_proxy,
	                                              NOTIFICATION_ITEM_DBUS_IFACE,
	                                              get_all_properties_cb,
	                                              app);

	/* We're returning, nothing is yet added until the properties
	   come back and give us more info. */
	return;
}

/* Removes an application.  Currently only works for the apps
   that are shown.  /TODO Need to fix that. */
void
application_service_appstore_application_remove (ApplicationServiceAppstore * appstore, const gchar * dbus_name, const gchar * dbus_object)
{
	g_return_if_fail(IS_APPLICATION_SERVICE_APPSTORE(appstore));
	g_return_if_fail(dbus_name != NULL && dbus_name[0] != '\0');
	g_return_if_fail(dbus_object != NULL && dbus_object[0] != '\0');

	ApplicationServiceAppstorePrivate * priv = appstore->priv;
	GList * listpntr;

	for (listpntr = priv->applications; listpntr != NULL; listpntr = g_list_next(listpntr)) {
		Application * app = (Application *)listpntr->data;

		if (!g_strcmp0(app->dbus_name, dbus_name) && !g_strcmp0(app->dbus_object, dbus_object)) {
			application_removed_cb(NULL, app);
			break; /* NOTE: Must break as the list will become inconsistent */
		}
	}

	return;
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
static gboolean
_application_service_server_get_applications (ApplicationServiceAppstore * appstore, GPtrArray ** apps, GError ** error)
{
	ApplicationServiceAppstorePrivate * priv = appstore->priv;

	*apps = g_ptr_array_new();
	GList * listpntr;
	gint position = 0;

	for (listpntr = priv->applications; listpntr != NULL; listpntr = g_list_next(listpntr)) {
		Application * app = (Application *)listpntr->data;
		GValueArray * values = g_value_array_new(5);

		GValue value = {0};

		/* Icon name */
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, app->icon);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* Position */
		g_value_init(&value, G_TYPE_INT);
		g_value_set_int(&value, position++);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* DBus Address */
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, app->dbus_name);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* DBus Object */
		g_value_init(&value, DBUS_TYPE_G_OBJECT_PATH);
		g_value_set_static_boxed(&value, app->menu);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* Icon path */
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, app->icon_theme_path);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* Label */
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, app->label);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		/* Guide */
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, app->guide);
		g_value_array_append(values, &value);
		g_value_unset(&value);

		g_ptr_array_add(*apps, values);
	}

	return TRUE;
}

/* Frees the data associated with an approver */
static void
approver_free (gpointer papprover, gpointer user_data)
{
	Approver * approver = (Approver *)papprover;
	g_return_if_fail(approver != NULL);
	
	if (approver->proxy != NULL) {
		g_object_unref(approver->proxy);
		approver->proxy = NULL;
	}

	g_free(approver);
	return;
}

/* What did the approver tell us? */
static void
approver_request_cb (DBusGProxy *proxy, gboolean OUT_approved, GError *error, gpointer userdata)
{
	g_debug("Approver responded: %s", OUT_approved ? "approve" : "rejected");
	return;
}

/* Run the applications through the new approver */
static void
check_with_new_approver (gpointer papp, gpointer papprove)
{
	Application * app = (Application *)papp;
	Approver * approver = (Approver *)papprove;

	org_ayatana_StatusNotifierApprover_approve_item_async(approver->proxy,
	                                                      app->id,
	                                                      app->category,
	                                                      0,
	                                                      app->dbus_name,
	                                                      app->dbus_object,
	                                                      approver_request_cb,
	                                                      app);

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

	GError * error = NULL;
	approver->proxy = dbus_g_proxy_new_for_name_owner(priv->bus,
	                                                  dbus_name,
	                                                  dbus_object,
	                                                  NOTIFICATION_APPROVER_DBUS_IFACE,
	                                                  &error);
	if (error != NULL) {
		g_warning("Unable to get approver interface on '%s:%s' : %s", dbus_name, dbus_object, error->message);
		g_error_free(error);
		g_free(approver);
		return;
	}

	priv->approvers = g_list_prepend(priv->approvers, approver);

	g_list_foreach(priv->applications, check_with_new_approver, approver);

	return;
}

