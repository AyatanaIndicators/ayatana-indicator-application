/*
The indicator application visualization object.  It takes the information
given by the service and turns it into real-world pixels that users can
actually use.  Well, GTK does that, but this asks nicely.

Copyright 2009 Canonical Ltd.
Copyright 2024-2025 Robert Tari

Authors:
    Ted Gould <ted@canonical.com>
    Robert Tari <robert@tari.in>

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

/* G Stuff */
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

/* DBus Stuff */
#include <libdbusmenu-gtk/menu.h>

/* Indicator Stuff */
#include <libayatana-indicator/indicator.h>
#include <libayatana-indicator/indicator-object.h>
#include <libayatana-indicator/indicator-image-helper.h>

/* Local Stuff */
#include "dbus-shared.h"
#include "gen-ayatana-application-service.xml.h"
#include "ayatana-application-service-marshal.h"

#define PANEL_ICON_SUFFIX  "panel"

#define INDICATOR_APPLICATION_TYPE            (indicator_application_get_type ())
#define INDICATOR_APPLICATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), INDICATOR_APPLICATION_TYPE, IndicatorApplication))
#define INDICATOR_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), INDICATOR_APPLICATION_TYPE, IndicatorApplicationClass))
#define IS_INDICATOR_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), INDICATOR_APPLICATION_TYPE))
#define IS_INDICATOR_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), INDICATOR_APPLICATION_TYPE))
#define INDICATOR_APPLICATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), INDICATOR_APPLICATION_TYPE, IndicatorApplicationClass))

typedef struct _IndicatorApplication      IndicatorApplication;
typedef struct _IndicatorApplicationClass IndicatorApplicationClass;

struct _IndicatorApplicationClass {
    IndicatorObjectClass parent_class;
};

struct _IndicatorApplication {
    IndicatorObject parent;
};

GType indicator_application_get_type (void);

INDICATOR_SET_VERSION
INDICATOR_SET_TYPE(INDICATOR_APPLICATION_TYPE)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct {
    GCancellable * service_proxy_cancel;
    GDBusProxy * service_proxy;
    GList * applications;
    GHashTable * theme_dirs;
    guint disconnect_kill;
    GCancellable * get_apps_cancel;
    guint watch;
    GDBusConnection *pConnection;
} IndicatorApplicationPrivate;

typedef struct _ApplicationEntry ApplicationEntry;
struct _ApplicationEntry {
    IndicatorObjectEntry entry;
    gchar * icon_theme_path;
    gboolean old_service;
    gchar * dbusobject;
    gchar * dbusaddress;
    gchar * guide;
    gchar * longname;
    gint nPosition;
    GMenuModel *pModel;
    GActionGroup *pActions;
    gboolean bMenuShown;
    gchar *sTooltipIcon;
    gchar *sTooltipMarkup;
};

static void indicator_application_class_init (IndicatorApplicationClass *klass);
static void indicator_application_init       (IndicatorApplication *self);
static void indicator_application_dispose    (GObject *object);
static void indicator_application_finalize   (GObject *object);
static GList * get_entries (IndicatorObject * io);
static guint get_location (IndicatorObject * io, IndicatorObjectEntry * entry);
static void entry_scrolled (IndicatorObject * io, IndicatorObjectEntry * entry, gint delta, IndicatorScrollDirection direction);
static void entry_secondary_activate (IndicatorObject * io, IndicatorObjectEntry * entry, guint time, gpointer data);
static void connected (GDBusConnection * con, const gchar * name, const gchar * owner, gpointer user_data);
static void disconnected (GDBusConnection * con, const gchar * name, gpointer user_data);
static void disconnected_helper (gpointer data, gpointer user_data);
static gboolean disconnected_kill (gpointer user_data);
static void disconnected_kill_helper (gpointer data, gpointer user_data);
static void application_added (IndicatorApplication * application, const gchar * iconname, gint position, const gchar * dbusaddress, const gchar * dbusobject, const gchar * icon_theme_path, const gchar * label, const gchar * guide, const gchar * accessible_desc, const gchar * hint, const gchar *sTooltipIcon, const gchar *sTooltipTitle, const gchar *sTooltipDescription);
static void application_removed (IndicatorApplication * application, gint position);
static void application_label_changed (IndicatorApplication * application, gint position, const gchar * label, const gchar * guide);
static void application_icon_changed (IndicatorApplication * application, gint position, const gchar * iconname, const gchar * icondesc);
static void application_icon_theme_path_changed (IndicatorApplication * application, gint position, const gchar * icon_theme_path);
static void get_applications (GObject * obj, GAsyncResult * res, gpointer user_data);
static void get_applications_helper (IndicatorApplication * self, GVariant * variant);
static void theme_dir_unref(IndicatorApplication * ia, const gchar * dir);
static void theme_dir_ref(IndicatorApplication * ia, const gchar * dir);
static void icon_theme_remove_dir_from_search_path (const char * dir);
static void service_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data);
static void receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name, GVariant * parameters, gpointer user_data);

G_DEFINE_TYPE_WITH_PRIVATE (IndicatorApplication, indicator_application, INDICATOR_OBJECT_TYPE);

static void
indicator_application_class_init (IndicatorApplicationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = indicator_application_dispose;
    object_class->finalize = indicator_application_finalize;

    IndicatorObjectClass * io_class = INDICATOR_OBJECT_CLASS(klass);

    io_class->get_entries = get_entries;
    io_class->get_location = get_location;
    io_class->secondary_activate = entry_secondary_activate;
    io_class->entry_scrolled = entry_scrolled;

    return;
}

static void
indicator_application_init (IndicatorApplication *self)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(self);

    /* These are built in the connection phase */
    priv->service_proxy_cancel = NULL;
    priv->service_proxy = NULL;
    priv->theme_dirs = NULL;
    priv->disconnect_kill = 0;

    priv->watch = g_bus_watch_name(G_BUS_TYPE_SESSION,
        INDICATOR_APPLICATION_DBUS_ADDR,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        connected,
        disconnected,
        self,
        NULL);

    priv->applications = NULL;
    priv->pConnection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    priv->theme_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    priv->get_apps_cancel = NULL;

    return;
}

static void
indicator_application_dispose (GObject *object)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(object));

    if (priv->disconnect_kill != 0) {
        g_source_remove(priv->disconnect_kill);
    }

    if (priv->get_apps_cancel != NULL) {
        g_cancellable_cancel(priv->get_apps_cancel);
        g_object_unref(priv->get_apps_cancel);
        priv->get_apps_cancel = NULL;
    }

    while (priv->applications != NULL) {
        application_removed(INDICATOR_APPLICATION(object),
                            0);
    }

    if (priv->service_proxy != NULL) {
        g_object_unref(G_OBJECT(priv->service_proxy));
        priv->service_proxy = NULL;
    }

    if (priv->service_proxy_cancel != NULL) {
        g_cancellable_cancel(priv->service_proxy_cancel);
        g_object_unref(priv->service_proxy_cancel);
        priv->service_proxy_cancel = NULL;
    }

    if (priv->theme_dirs != NULL) {
        gpointer directory;
        GHashTableIter iter;
        g_hash_table_iter_init (&iter, priv->theme_dirs);
        while (g_hash_table_iter_next (&iter, &directory, NULL)) {
            icon_theme_remove_dir_from_search_path (directory);
        }
        g_hash_table_destroy(priv->theme_dirs);
        priv->theme_dirs = NULL;
    }

    if (priv->watch != 0) {
        g_bus_unwatch_name(priv->watch);
        priv->watch = 0;
    }

    g_clear_object (&priv->pConnection);

    G_OBJECT_CLASS (indicator_application_parent_class)->dispose (object);
    return;
}

static void
indicator_application_finalize (GObject *object)
{

    G_OBJECT_CLASS (indicator_application_parent_class)->finalize (object);
    return;
}

/* Brings up the connection to a service that has just come onto the
   bus, or is atleast new to us. */
static void
connected (GDBusConnection * con, const gchar * name, const gchar * owner, gpointer user_data)
{
    IndicatorApplication * application = INDICATOR_APPLICATION(user_data);
    g_return_if_fail(application != NULL);

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);

    g_debug("Connected to Application Indicator Service.");

    if (priv->service_proxy_cancel == NULL && priv->service_proxy == NULL) {
        /* Build the service proxy */
        priv->service_proxy_cancel = g_cancellable_new();

        g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION,
                         G_DBUS_PROXY_FLAGS_NONE,
                         NULL,
                                 INDICATOR_APPLICATION_DBUS_ADDR,
                                 INDICATOR_APPLICATION_DBUS_OBJ,
                                 INDICATOR_APPLICATION_DBUS_IFACE,
                         priv->service_proxy_cancel,
                         service_proxy_cb,
                             application);
    }

    return;
}

/* Callback from trying to create the proxy for the service, this
   could include starting the service. */
static void
service_proxy_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
    GError * error = NULL;

    IndicatorApplication * self = INDICATOR_APPLICATION(user_data);
    g_return_if_fail(self != NULL);

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(self);

    GDBusProxy * proxy = g_dbus_proxy_new_for_bus_finish(res, &error);

    if (priv->service_proxy_cancel != NULL) {
        g_object_unref(priv->service_proxy_cancel);
        priv->service_proxy_cancel = NULL;
    }

    if (error != NULL) {
        g_critical("Could not grab DBus proxy for %s: %s", INDICATOR_APPLICATION_DBUS_ADDR, error->message);
        g_error_free(error);
        return;
    }

    /* Okay, we're good to grab the proxy at this point, we're
    sure that it's ours. */
    priv->service_proxy = proxy;

    g_signal_connect(proxy, "g-signal", G_CALLBACK(receive_signal), self);

    /* We shouldn't be in a situation where we've already
       called this function.  It doesn't *hurt* anything, but
       man we should look into it more. */
    if (priv->get_apps_cancel != NULL) {
        g_warning("Already getting applications?  Odd.");
        return;
    }

    priv->get_apps_cancel = g_cancellable_new();

    /* Query it for existing applications */
    g_debug("Request current apps");
    g_dbus_proxy_call(priv->service_proxy, "GetApplications", NULL,
                      G_DBUS_CALL_FLAGS_NONE, -1, priv->get_apps_cancel,
                      get_applications, self);

    return;
}

/* Marks every current application as belonging to the old
   service so that we can delete it if it doesn't come back.
   Also, sets up a timeout on comming back. */
static void
disconnected (GDBusConnection * con, const gchar * name, gpointer user_data)
{
    IndicatorApplication * application = INDICATOR_APPLICATION(user_data);
    g_return_if_fail(application != NULL);

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);
    g_list_foreach(priv->applications, disconnected_helper, application);
    /* I'll like this to be a little shorter, but it's a bit
       inpractical to make it so.  This means that the user will
       probably notice a visible glitch.  Though, if applications
       are disappearing there isn't much we can do. */
    priv->disconnect_kill = g_timeout_add(250, disconnected_kill, application);
    return;
}

/* Marks an entry as being from the old service */
static void
disconnected_helper (gpointer data, gpointer user_data)
{
    ApplicationEntry * entry = (ApplicationEntry *)data;
    entry->old_service = TRUE;
    return;
}

/* Makes sure the old applications that don't come back
   get dropped. */
static gboolean
disconnected_kill (gpointer user_data)
{
    g_return_val_if_fail(IS_INDICATOR_APPLICATION(user_data), FALSE);
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(user_data));

    priv->disconnect_kill = 0;
    g_list_foreach(priv->applications, disconnected_kill_helper, user_data);
    return FALSE;
}

/* Looks for entries that are still associated with the
   old service and removes them. */
static void
disconnected_kill_helper (gpointer data, gpointer user_data)
{
    g_return_if_fail(IS_INDICATOR_APPLICATION(user_data));
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(user_data));
    ApplicationEntry * entry = (ApplicationEntry *)data;
    if (entry->old_service) {
        application_removed(INDICATOR_APPLICATION(user_data), g_list_index(priv->applications, data));
    }
    return;
}

/* Goes through the list of applications that we're maintaining and
   pulls out the IndicatorObjectEntry and returns that in a list
   for the caller. */
static GList *
get_entries (IndicatorObject * io)
{
    g_return_val_if_fail(IS_INDICATOR_APPLICATION(io), NULL);

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(io));

    GList * retval = NULL;
    GList * apppointer = NULL;

    for (apppointer = priv->applications; apppointer != NULL; apppointer = g_list_next(apppointer)) {
        IndicatorObjectEntry * entry = &(((ApplicationEntry *)apppointer->data)->entry);
        retval = g_list_prepend(retval, entry);
    }

    if (retval != NULL) {
        retval = g_list_reverse(retval);
    }

    return retval;
}

/* Finds the location of a specific entry */
static guint
get_location (IndicatorObject * io, IndicatorObjectEntry * entry)
{
    g_return_val_if_fail(IS_INDICATOR_APPLICATION(io), 0);
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(io));
    return g_list_index(priv->applications, entry);
}

/* Redirect the secondary activate to the Application Item */
static void
entry_secondary_activate (IndicatorObject * io, IndicatorObjectEntry * entry,
                          guint time, gpointer data)
{
    g_return_if_fail(IS_INDICATOR_APPLICATION(io));

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(io));
    g_return_if_fail(priv->service_proxy);

    GList *l = g_list_find(priv->applications, entry);
    if (l == NULL)
        return;

    ApplicationEntry *app = l->data;

    if (app && app->dbusaddress && app->dbusobject && priv->service_proxy) {
        g_dbus_proxy_call(priv->service_proxy, "ApplicationSecondaryActivateEvent",
                          g_variant_new("(ssu)", app->dbusaddress,
                                                 app->dbusobject,
                                                 time),
                          G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

/* Redirect the scroll event to the Application Item */
static void entry_scrolled (IndicatorObject * io, IndicatorObjectEntry * entry, gint delta, IndicatorScrollDirection direction)
{
    g_return_if_fail(IS_INDICATOR_APPLICATION(io));

    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(INDICATOR_APPLICATION(io));
    g_return_if_fail(priv->service_proxy);

    GList *l = g_list_find(priv->applications, entry);
    if (l == NULL)
        return;

    ApplicationEntry *app = l->data;

    if (app && app->dbusaddress && app->dbusobject && priv->service_proxy) {
        g_dbus_proxy_call(priv->service_proxy, "ApplicationScrollEvent",
                          g_variant_new("(ssiu)", app->dbusaddress,
                                                  app->dbusobject,
                                                  delta, direction),
                          G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

/* Does a quick meausre of how big the string is in
   pixels with a Pango layout */
static gint
measure_string (GtkStyleContext *pContext, PangoContext * context, const gchar * string)
{
    PangoLayout * layout = pango_layout_new(context);
    pango_layout_set_text(layout, string, -1);
    PangoFontDescription *pDescription = NULL;
    GtkStateFlags nFlags = gtk_style_context_get_state (pContext);
    gtk_style_context_get (pContext, nFlags, GTK_STYLE_PROPERTY_FONT, &pDescription, NULL);

    if (pDescription)
    {
        pango_layout_set_font_description(layout, pDescription);
        pango_font_description_free (pDescription);
    }

    gint width;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

/* Try to get a good guess at what a maximum width of the entire
   string would be. */
static void
guess_label_size (ApplicationEntry * app)
{
    /* This is during startup. */
    if (app->entry.label == NULL) return;

    GtkStyleContext *pContext = gtk_widget_get_style_context (GTK_WIDGET (app->entry.label));
    PangoContext * context = gtk_widget_get_pango_context(GTK_WIDGET(app->entry.label));

    gint length = measure_string(pContext, context, gtk_label_get_text(app->entry.label));

    if (app->guide != NULL) {
        gint guidelen = measure_string(pContext, context, app->guide);
        if (guidelen > length) {
            length = guidelen;
        }
    }

    gtk_widget_set_size_request(GTK_WIDGET(app->entry.label), length, -1);

    return;
}

static void onMenuPoppedUp (GtkWidget *pWidget, gpointer pFlippedRect, gpointer pFinalRect, gboolean bFlippedX, gboolean bFlippedY, gpointer pData)
{
    ApplicationEntry *pEntry = (ApplicationEntry*) pData;
    pEntry->bMenuShown = TRUE;
}

static void onMenuHide (GtkWidget *pWidget, gpointer pData)
{
    ApplicationEntry *pEntry = (ApplicationEntry*) pData;
    pEntry->bMenuShown = FALSE;
}

static gboolean onQueryTooltip (GtkWidget *pWidget, gint nX, gint nY, gboolean bKeyboardMode, GtkTooltip *pTooltip, gpointer pData)
{
    ApplicationEntry *pEntry = (ApplicationEntry*) pData;

    if (!pEntry->bMenuShown && pEntry->sTooltipMarkup)
    {
        gtk_tooltip_set_markup (pTooltip, pEntry->sTooltipMarkup);

        if (pEntry->sTooltipIcon)
        {
            gtk_tooltip_set_icon_from_icon_name (pTooltip, pEntry->sTooltipIcon, GTK_ICON_SIZE_LARGE_TOOLBAR);
        }

        return TRUE;
    }

    return FALSE;
}

static gboolean onTooltipConnect (gpointer pData)
{
    ApplicationEntry *pEntry = (ApplicationEntry*) pData;

    if (pEntry->entry.label)
    {
        gtk_widget_set_has_tooltip (GTK_WIDGET (pEntry->entry.label), TRUE);
        g_signal_connect (pEntry->entry.label, "query-tooltip", G_CALLBACK (onQueryTooltip), pEntry);
    }

    if (pEntry->entry.image)
    {
        gtk_widget_set_has_tooltip (GTK_WIDGET (pEntry->entry.image), TRUE);
        g_signal_connect (pEntry->entry.image, "query-tooltip", G_CALLBACK (onQueryTooltip), pEntry);
    }

    return G_SOURCE_REMOVE;
}

static void applicationAddedFinish (ApplicationEntry *pEntry)
{
    /* Keep copies of these for ourself, just in case. */
    g_object_ref (pEntry->entry.image);
    g_object_ref (pEntry->entry.menu);

    gtk_widget_show (GTK_WIDGET (pEntry->entry.image));
    gtk_widget_hide (GTK_WIDGET (pEntry->entry.menu));
    IndicatorApplicationPrivate * pPrivate = indicator_application_get_instance_private (INDICATOR_APPLICATION (pEntry->entry.parent_object));
    pPrivate->applications = g_list_insert (pPrivate->applications, pEntry, pEntry->nPosition);
    g_signal_emit (G_OBJECT (pEntry->entry.parent_object), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED_ID, 0, &(pEntry->entry), TRUE);
    g_signal_connect (pEntry->entry.menu, "popped-up", G_CALLBACK (onMenuPoppedUp), pEntry);
    g_signal_connect (pEntry->entry.menu, "hide", G_CALLBACK (onMenuHide), pEntry);

    // Make sure our widgets are constructed and displayed
    g_timeout_add_seconds (2, onTooltipConnect, pEntry);
}

static void onMenuModelChanged (GMenuModel *pModel, gint nPosition, gint nRemoved, gint nAdded, gpointer pData)
{
    ApplicationEntry *pEntry = (ApplicationEntry*) pData;
    g_signal_handlers_disconnect_by_data (pEntry->pModel, pEntry);
    pEntry->entry.menu = GTK_MENU (gtk_menu_new_from_model (G_MENU_MODEL (pModel)));
    gtk_menu_shell_bind_model (GTK_MENU_SHELL (pEntry->entry.menu), pModel, NULL, TRUE);
    IndicatorApplicationPrivate *pPrivate = indicator_application_get_instance_private (INDICATOR_APPLICATION (pEntry->entry.parent_object));
    pEntry->pActions = G_ACTION_GROUP (g_dbus_action_group_get (pPrivate->pConnection, pEntry->dbusaddress, pEntry->dbusobject));
    gtk_widget_insert_action_group (GTK_WIDGET (pEntry->entry.menu), "indicator", pEntry->pActions);
    applicationAddedFinish (pEntry);
}

static void setTooltip (ApplicationEntry *pEntry, const gchar *sTooltipIcon, const gchar *sTooltipTitle, const gchar *sTooltipDescription)
{
    g_free (pEntry->sTooltipIcon);
    pEntry->sTooltipIcon = NULL;
    g_free (pEntry->sTooltipMarkup);
    pEntry->sTooltipMarkup = NULL;

    if (sTooltipTitle && sTooltipTitle[0] != '\0' && sTooltipDescription && sTooltipDescription[0] != '\0')
    {
        pEntry->sTooltipMarkup = g_strdup_printf ("<b>%s</b>\n\n%s", sTooltipTitle, sTooltipDescription);
    }
    else if (sTooltipTitle && sTooltipTitle[0] != '\0')
    {
        pEntry->sTooltipMarkup = g_strdup (sTooltipTitle);
    }
    else if (sTooltipDescription && sTooltipDescription[0] != '\0')
    {
        pEntry->sTooltipMarkup = g_strdup (sTooltipDescription);
    }

    if (sTooltipIcon && sTooltipIcon[0] != '\0')
    {
        pEntry->sTooltipIcon = g_strdup (sTooltipIcon);
    }
}

/* Here we respond to new applications by building up the
   ApplicationEntry and signaling the indicator host that
   we've got a new indicator. */
static void
application_added (IndicatorApplication * application, const gchar * iconname, gint position, const gchar * dbusaddress, const gchar * dbusobject, const gchar * icon_theme_path, const gchar * label, const gchar * guide, const gchar * accessible_desc, const gchar * hint, const gchar *sTooltipIcon, const gchar *sTooltipTitle, const gchar *sTooltipDescription)
{
    g_return_if_fail(IS_INDICATOR_APPLICATION(application));
    g_debug("Building new application entry: %s  with icon: %s at position %i", dbusaddress, iconname, position);
    ApplicationEntry * app = g_new0(ApplicationEntry, 1);

    app->bMenuShown = FALSE;
    app->entry.parent_object = INDICATOR_OBJECT(application);
    app->nPosition = position;
    app->old_service = FALSE;
    app->icon_theme_path = NULL;
    if (icon_theme_path != NULL && icon_theme_path[0] != '\0') {
        app->icon_theme_path = g_strdup(icon_theme_path);
        theme_dir_ref(application, icon_theme_path);
    }

    app->dbusaddress = g_strdup(dbusaddress);
    app->dbusobject = g_strdup(dbusobject);
    app->guide = NULL;

    /* We make a long name using the suffix, and if that
       icon is available we want to use it.  Otherwise we'll
       just use the name we were given. */
    app->longname = NULL;
    if (!g_str_has_suffix(iconname, PANEL_ICON_SUFFIX)) {
        app->longname = g_strdup_printf("%s-%s", iconname, PANEL_ICON_SUFFIX);
    } else {
        app->longname = g_strdup(iconname);
    }
    app->entry.image = indicator_image_helper(app->longname);

    if (label == NULL || label[0] == '\0') {
        app->entry.label = NULL;
    } else {
        app->entry.label = GTK_LABEL(gtk_label_new(label));
        g_object_ref(G_OBJECT(app->entry.label));
        gtk_widget_show(GTK_WIDGET(app->entry.label));

        if (app->guide != NULL) {
            g_free(app->guide);
            app->guide = NULL;
        }

        if (guide != NULL) {
            app->guide = g_strdup(guide);
        }

        guess_label_size(app);
    }

    if (accessible_desc == NULL || accessible_desc[0] == '\0') {
        app->entry.accessible_desc = NULL;
    } else {
        app->entry.accessible_desc = g_strdup(accessible_desc);
    }

    if (hint == NULL || hint[0] == '\0') {
        app->entry.name_hint = NULL;
    } else {
        app->entry.name_hint = g_strdup(hint);
    }

    setTooltip (app, sTooltipIcon, sTooltipTitle, sTooltipDescription);
    gboolean bGLibMenu = g_str_has_prefix (dbusobject, "/org/ayatana/appindicator/");

    if (bGLibMenu)
    {
        IndicatorApplicationPrivate * pPrivate = indicator_application_get_instance_private (application);
        app->pModel = G_MENU_MODEL (g_dbus_menu_model_get (pPrivate->pConnection, dbusaddress, dbusobject));
        g_signal_connect (app->pModel, "items-changed", G_CALLBACK (onMenuModelChanged), app);

        if (g_menu_model_get_n_items (app->pModel))
        {
            onMenuModelChanged (app->pModel, 0, 0, 1, app);
        }
    }
    else
    {
        app->entry.menu = GTK_MENU (dbusmenu_gtkmenu_new ((gchar*) dbusaddress, (gchar*) dbusobject));
        applicationAddedFinish (app);
    }
}

/* This removes the application from the list and free's all
   of the memory associated with it. */
static void
application_removed (IndicatorApplication * application, gint position)
{
    g_return_if_fail(IS_INDICATOR_APPLICATION(application));
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);
    ApplicationEntry * app = (ApplicationEntry *)g_list_nth_data(priv->applications, position);

    if (app == NULL) {
        g_warning("Unable to find application at position: %d", position);
        return;
    }

    priv->applications = g_list_remove(priv->applications, app);
    g_signal_emit(G_OBJECT(application), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED_ID, 0, &(app->entry), TRUE);

    if (app->icon_theme_path != NULL) {
        theme_dir_unref(application, app->icon_theme_path);
        g_free(app->icon_theme_path);
    }
    if (app->dbusaddress != NULL) {
        g_free(app->dbusaddress);
    }
    if (app->dbusobject != NULL) {
        g_free(app->dbusobject);
    }
    if (app->guide != NULL) {
        g_free(app->guide);
    }
    if (app->longname != NULL) {
        g_free(app->longname);
    }
    if (app->entry.image != NULL) {
        g_object_unref(G_OBJECT(app->entry.image));
    }
    if (app->entry.label != NULL) {
        g_object_unref(G_OBJECT(app->entry.label));
    }
    if (app->entry.menu != NULL) {
        g_object_unref(G_OBJECT(app->entry.menu));
    }
    if (app->entry.accessible_desc != NULL) {
        g_free((gchar *)app->entry.accessible_desc);
    }
    if (app->entry.name_hint != NULL) {
        g_free((gchar *)app->entry.name_hint);
    }

    g_free (app->sTooltipIcon);
    g_free (app->sTooltipMarkup);
    g_clear_object (&app->pModel);
    g_clear_object (&app->pActions);
    g_free(app);

    return;
}

/* The callback for the signal that the label for an application
   has changed. */
static void
application_label_changed (IndicatorApplication * application, gint position, const gchar * label, const gchar * guide)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);
    ApplicationEntry * app = (ApplicationEntry *)g_list_nth_data(priv->applications, position);
    gboolean signal_reload = FALSE;

    if (app == NULL) {
        g_warning("Unable to find application at position: %d", position);
        return;
    }

    if (label == NULL || label[0] == '\0') {
        /* No label, let's see if we need to delete the old one */
        if (app->entry.label != NULL) {
            g_object_unref(G_OBJECT(app->entry.label));
            app->entry.label = NULL;

            signal_reload = TRUE;
        }
    } else {
        /* We've got a label, is this just an update or is
           it a new thing. */
        if (app->entry.label != NULL) {
            gtk_label_set_text(app->entry.label, label);
        } else {
            app->entry.label = GTK_LABEL(gtk_label_new(label));
            gtk_widget_set_has_tooltip (GTK_WIDGET (app->entry.label), TRUE);
            g_signal_connect (app->entry.label, "query-tooltip", G_CALLBACK (onQueryTooltip), app);
            g_object_ref(G_OBJECT(app->entry.label));
            gtk_widget_show(GTK_WIDGET(app->entry.label));

            signal_reload = TRUE;
        }
    }

    /* Copy the guide if we have one */
    if (app->guide != NULL) {
        g_free(app->guide);
        app->guide = NULL;
    }

    if (guide != NULL && guide[0] != '\0') {
        app->guide = g_strdup(guide);
    }

    /* Protected against not having a label */
    guess_label_size(app);

    if (signal_reload) {
        /* Telling the listener that this has been removed, and then
           readded to make it reparse the entry. */
        if (app->entry.label != NULL) {
            gtk_widget_hide(GTK_WIDGET(app->entry.label));
        }

        if (app->entry.image != NULL) {
            gtk_widget_hide(GTK_WIDGET(app->entry.image));
        }

        if (app->entry.menu != NULL) {
            gtk_menu_detach(app->entry.menu);
        }

        g_signal_emit(G_OBJECT(application), INDICATOR_OBJECT_SIGNAL_ENTRY_REMOVED_ID, 0, &(app->entry), TRUE);

        if (app->entry.label != NULL) {
            gtk_widget_show(GTK_WIDGET(app->entry.label));
        }

        if (app->entry.image != NULL) {
            indicator_image_helper_update(app->entry.image, app->longname);
            gtk_widget_show(GTK_WIDGET(app->entry.image));
        }

        g_signal_emit(G_OBJECT(application), INDICATOR_OBJECT_SIGNAL_ENTRY_ADDED_ID, 0, &(app->entry), TRUE);
    }

    return;
}

/* The callback for the signal that the icon for an application
   has changed. */
static void
application_icon_changed (IndicatorApplication * application, gint position, const gchar * iconname, const gchar * icondesc)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);
    ApplicationEntry * app = (ApplicationEntry *)g_list_nth_data(priv->applications, position);

    if (app == NULL) {
        g_warning("Unable to find application at position: %d", position);
        return;
    }

    if (iconname == NULL) {
        g_warning("We can't have a NULL icon name on %d", position);
        return;
    }

    /* We make a long name using the suffix, and if that
       icon is available we want to use it.  Otherwise we'll
       just use the name we were given. */
    if (app->longname != NULL) {
        g_free(app->longname);
        app->longname = NULL;
    }
    if (!g_str_has_suffix(iconname, PANEL_ICON_SUFFIX)) {
        app->longname = g_strdup_printf("%s-%s", iconname, PANEL_ICON_SUFFIX);
    } else {
        app->longname = g_strdup(iconname);
    }
    indicator_image_helper_update(app->entry.image, app->longname);

    if (g_strcmp0(app->entry.accessible_desc, icondesc) != 0) {
        if (app->entry.accessible_desc != NULL) {
            g_free((gchar *)app->entry.accessible_desc);
            app->entry.accessible_desc = NULL;
        }

        if (icondesc != NULL && icondesc[0] != '\0') {
            app->entry.accessible_desc = g_strdup(icondesc);
        }

        g_signal_emit(G_OBJECT(application), INDICATOR_OBJECT_SIGNAL_ACCESSIBLE_DESC_UPDATE_ID, 0, &(app->entry), TRUE);
    }

    return;
}

/* The callback for the signal that the icon theme path for an application
   has changed. */
static void
application_icon_theme_path_changed (IndicatorApplication * application, gint position, const gchar * icon_theme_path)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(application);
    ApplicationEntry * app = (ApplicationEntry *)g_list_nth_data(priv->applications, position);

    if (app == NULL) {
        g_warning("Unable to find application at position: %d", position);
        return;
    }

    if (g_strcmp0(icon_theme_path, app->icon_theme_path) != 0) {
        if(app->icon_theme_path != NULL) {
            theme_dir_unref(application, app->icon_theme_path);
            g_free(app->icon_theme_path);
            app->icon_theme_path = NULL;
        }
        if (icon_theme_path != NULL && icon_theme_path[0] != '\0') {
            app->icon_theme_path = g_strdup(icon_theme_path);
            theme_dir_ref(application, app->icon_theme_path);
        }
       indicator_image_helper_update(app->entry.image, app->longname);
    }

    return;
}

/* Receives all signals from the service, routed to the appropriate functions */
static void
receive_signal (GDBusProxy * proxy, gchar * sender_name, gchar * signal_name,
                GVariant * parameters, gpointer user_data)
{
    IndicatorApplication * self = INDICATOR_APPLICATION(user_data);
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(self);

    /* If we're in the middle of a GetApplications call and we get
       any of these our state is probably going to just be confused.  Let's
       cancel the call we had and try again to try and get a clear answer */
    if (priv->get_apps_cancel != NULL) {
        g_cancellable_cancel(priv->get_apps_cancel);
        g_object_unref(priv->get_apps_cancel);

        priv->get_apps_cancel = g_cancellable_new();

        g_dbus_proxy_call(priv->service_proxy, "GetApplications", NULL,
                          G_DBUS_CALL_FLAGS_NONE, -1, priv->get_apps_cancel,
                          get_applications, self);
        return;
    }

    if (g_strcmp0(signal_name, "ApplicationAdded") == 0) {
        gchar * iconname = NULL;
        gint position;
        gchar * dbusaddress = NULL;
        gchar * dbusobject = NULL;
        gchar * icon_theme_path = NULL;
        gchar * label = NULL;
        gchar * guide = NULL;
        gchar * accessible_desc = NULL;
        gchar * hint = NULL;
        gchar * title = NULL;
        gchar *sTooltipIcon = NULL;
        gchar *sTooltipTitle = NULL;
        gchar *sTooltipDescription = NULL;
        g_variant_get (parameters, "(sisosssssssss)", &iconname,
                       &position, &dbusaddress, &dbusobject,
                       &icon_theme_path, &label, &guide,
                       &accessible_desc, &hint, &title, &sTooltipIcon, &sTooltipTitle, &sTooltipDescription);

        application_added(self, iconname, position, dbusaddress,
                          dbusobject, icon_theme_path, label, guide,
                          accessible_desc, hint, sTooltipIcon, sTooltipTitle, sTooltipDescription);
        g_free(iconname);
        g_free(dbusaddress);
        g_free(dbusobject);
        g_free(icon_theme_path);
        g_free(label);
        g_free(guide);
        g_free(accessible_desc);
        g_free(hint);
        g_free(title);
        g_free (sTooltipIcon);
        g_free (sTooltipTitle);
        g_free (sTooltipDescription);
    }
    else if (g_strcmp0(signal_name, "ApplicationRemoved") == 0) {
        gint position;
        g_variant_get (parameters, "(i)", &position);
        application_removed(self, position);
    }
    else if (g_strcmp0(signal_name, "ApplicationIconChanged") == 0) {
        gint position;
        gchar * iconname = NULL;
        gchar * icondesc = NULL;
        g_variant_get (parameters, "(iss)", &position, &iconname, &icondesc);
        application_icon_changed(self, position, iconname, icondesc);
        g_free(iconname);
        g_free(icondesc);
    }
    else if (g_strcmp0(signal_name, "ApplicationIconThemePathChanged") == 0) {
        gint position;
        gchar * icon_theme_path = NULL;
        g_variant_get (parameters, "(is)", &position, &icon_theme_path);
        application_icon_theme_path_changed(self, position, icon_theme_path);
        g_free(icon_theme_path);
    }
    else if (g_strcmp0(signal_name, "ApplicationLabelChanged") == 0) {
        gint position;
        gchar * label = NULL;
        gchar * guide = NULL;
        g_variant_get (parameters, "(iss)", &position, &label, &guide);
        application_label_changed(self, position, label, guide);
        g_free(label);
        g_free(guide);
    }
    else if (g_strcmp0 (signal_name, "ApplicationTooltipChanged") == 0)
    {
        gint nPosition = 0;
        gchar *sIcon = NULL;
        gchar *sTitle = NULL;
        gchar *sDescription = NULL;
        g_variant_get (parameters, "(isss)", &nPosition, &sIcon, &sTitle, &sDescription);
        ApplicationEntry *pEntry = (ApplicationEntry*) g_list_nth_data (priv->applications, nPosition);
        setTooltip (pEntry, sIcon, sTitle, sDescription);
        g_free (sIcon);
        g_free (sTitle);
        g_free (sDescription);
    }

    return;
}

/* This responds to the list of applications that the service
   has and calls application_added on each one of them. */
static void
get_applications (GObject * obj, GAsyncResult * res, gpointer user_data)
{
    IndicatorApplication * self = INDICATOR_APPLICATION(user_data);
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(self);
    GError * error = NULL;
    GVariant * result;
    GVariant * child;
    GVariantIter * iter;

    result = g_dbus_proxy_call_finish(priv->service_proxy, res, &error);

    /* No one can cancel us anymore, we've completed! */
    if (priv->get_apps_cancel != NULL) {
        if (error == NULL || error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED) {
            g_object_unref(priv->get_apps_cancel);
            priv->get_apps_cancel = NULL;
        }
    }

    /* If we got an error, print it and exit out */
    if (error != NULL) {
        g_warning("Unable to get application list: %s", error->message);
        g_error_free(error);
        return;
    }

    /* Remove all applications that we previously had
       as we're going to repopulate the list. */
    while (priv->applications != NULL) {
        application_removed(self, 0);
    }

    /* Get our new applications that we got in the request */
    g_variant_get(result, "(a(sisosssssssss))", &iter);
    while ((child = g_variant_iter_next_value (iter))) {
        get_applications_helper(self, child);
        g_variant_unref(child);
    }
    g_variant_iter_free (iter);
    g_variant_unref(result);

    return;
}

/* A little helper that takes apart the DBus structure and calls
   application_added on every entry in the list. */
static void
get_applications_helper (IndicatorApplication * self, GVariant * variant)
{
    gchar * icon_name = NULL;
    gint position;
    gchar * dbus_address = NULL;
    gchar * dbus_object = NULL;
    gchar * icon_theme_path = NULL;
    gchar * label = NULL;
    gchar * guide = NULL;
    gchar * accessible_desc = NULL;
    gchar * hint = NULL;
    gchar * title = NULL;
    gchar *sTooltipIcon = NULL;
    gchar *sTooltipTitle = NULL;
    gchar *sTooltipDescription = NULL;
    g_variant_get(variant, "(sisosssssssss)", &icon_name, &position,
                  &dbus_address, &dbus_object, &icon_theme_path, &label,
                  &guide, &accessible_desc, &hint, &title, &sTooltipIcon, &sTooltipTitle, &sTooltipDescription);

    application_added(self, icon_name, position, dbus_address, dbus_object, icon_theme_path, label, guide, accessible_desc, hint, sTooltipIcon, sTooltipTitle, sTooltipDescription);

    g_free(icon_name);
    g_free(dbus_address);
    g_free(dbus_object);
    g_free(icon_theme_path);
    g_free(label);
    g_free(guide);
    g_free(accessible_desc);
    g_free(hint);
    g_free(title);
    g_free (sTooltipIcon);
    g_free (sTooltipTitle);
    g_free (sTooltipDescription);

    return;
}

/* Unrefs a theme directory.  This may involve removing it from
   the search path. */
static void
theme_dir_unref(IndicatorApplication * ia, const gchar * dir)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(ia);

    if (!g_hash_table_contains (priv->theme_dirs, dir)) {
        g_warning("Unref'd a directory '%s' that wasn't in the theme dir hash table.", dir);
    } else {
        int count = GPOINTER_TO_INT(g_hash_table_lookup(priv->theme_dirs, dir));
        if (count > 1) {
            count--;
            g_hash_table_insert(priv->theme_dirs, g_strdup(dir), GINT_TO_POINTER(count));
        } else {
            icon_theme_remove_dir_from_search_path (dir);
            g_hash_table_remove (priv->theme_dirs, dir);
        }
    }
}

static void
icon_theme_remove_dir_from_search_path (const char * dir)
{
    GtkIconTheme * icon_theme = gtk_icon_theme_get_default();
    gchar ** paths;
    gint path_count;

    gtk_icon_theme_get_search_path(icon_theme, &paths, &path_count);

    gint i;
    gboolean found = FALSE;
    for (i = 0; i < path_count; i++) {
        if (found) {
            /* If we've already found the right entry */
            paths[i - 1] = paths[i];
        } else {
            /* We're still looking, is this the one? */
            if (!g_strcmp0(paths[i], dir)) {
                found = TRUE;
                /* We're freeing this here as it won't be captured by the
                   g_strfreev() below as it's out of the array. */
                g_free(paths[i]);
            }
        }
    }

    /* If we found one we need to reset the path to
       accomidate the changes */
    if (found) {
        paths[path_count - 1] = NULL; /* Clear the last one */
        gtk_icon_theme_set_search_path(icon_theme, (const gchar **)paths, path_count - 1);
    }

    g_strfreev(paths);

    return;
}

/* Refs a theme directory, and it may add it to the search
   path */
static void
theme_dir_ref(IndicatorApplication * ia, const gchar * dir)
{
    IndicatorApplicationPrivate * priv = indicator_application_get_instance_private(ia);

    int count = 0;
    if ((count = GPOINTER_TO_INT(g_hash_table_lookup(priv->theme_dirs, dir))) != 0) {
        /* It exists so what we need to do is increase the ref
           count of this dir. */
        count++;
    } else {
        /* It doesn't exist, so we need to add it to the table
           and to the search path. */
        gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), dir);
        g_debug("\tAppending search path: %s", dir);
        count = 1;
    }

    g_hash_table_insert(priv->theme_dirs, g_strdup(dir), GINT_TO_POINTER(count));

    return;
}

