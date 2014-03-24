/* Copyright 2014 Endless Mobile, Inc. */

#include <gio/gio.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <eosmetrics/eosmetrics.h>

static GDBusArgInfo OBJ_ID_ARG_INFO =
{
    -1 /* ref_count */,
    "object_id",
    "s",
    NULL /* GDBusAnnotationInfo */
};

static GDBusArgInfo OBJ_PATH_ARG_INFO =
{
    -1 /* ref_count */,
    "object_path",
    "o",
    NULL /* GDBusAnnotationInfo */
};

static GDBusArgInfo BEFORE_SHUTDOWN_ARG_INFO =
{
    -1 /* ref_count */,
    "before_shutdown",
    "b",
    NULL /* GDBusAnnotationInfo */
};

static GDBusArgInfo *LOGIN_ARG_INFO[] =
{
    &OBJ_ID_ARG_INFO,
    &OBJ_PATH_ARG_INFO,
    NULL
};

static GDBusArgInfo *SHUTDOWN_ARG_INFO[] =
{
    &BEFORE_SHUTDOWN_ARG_INFO,
    NULL
};

static gchar LOGIN_SIGNAL_NAME[] = "SessionNew";

static GDBusSignalInfo LOGIN_SIGNAL_INFO =
{
    -1 /* ref_count */,
    LOGIN_SIGNAL_NAME,
    LOGIN_ARG_INFO,
    NULL /* GDBusAnnotationInfo */
};

static gchar SHUTDOWN_SIGNAL_NAME[] = "PrepareForShutdown";

static GDBusSignalInfo SHUTDOWN_SIGNAL_INFO =
{
    -1 /* ref_count */,
    SHUTDOWN_SIGNAL_NAME,
    SHUTDOWN_ARG_INFO,
    NULL /* GDBusAnnotationInfo */
};

static GDBusSignalInfo *LOGIN_SIGNAL_INFO_ARR[] =
{
    &LOGIN_SIGNAL_INFO,
    &SHUTDOWN_SIGNAL_INFO,
    NULL
};

static gchar LOGIN_MANAGER_INTERFACE_NAME[] = "org.freedesktop.login1.Manager";

static GDBusInterfaceInfo LOGIN_MANAGER_INTERFACE_INFO =
{
    -1 /* ref_count */,
    LOGIN_MANAGER_INTERFACE_NAME,
    NULL /* GDBusMethodInfo */,
    LOGIN_SIGNAL_INFO_ARR,
    NULL /* GDBusPropertyInfo */,
    NULL /* GDBusAnnotationInfo */
};

static const gchar LOGIN_BUS_NAME[] = "org.freedesktop.login1";
static const gchar LOGIN_OBJECT_PATH[] = "/org/freedesktop/login1";
static const gchar SIGNAL_NAME[] = "g-signal";

static EmtrEventRecorder *event_recorder;

static void
record_login (GDBusProxy *dbus_proxy, gchar *sender_name, gchar *signal_name,
  GVariant *parameters, gpointer user_data)
{
    if (strcmp(LOGIN_SIGNAL_NAME, signal_name) == 0) {
        gchar *object_id;
        g_variant_get_child (parameters, 0, "s", &object_id);
        // TODO: Use the object_id as a key.
        emtr_event_recorder_record_start (event_recorder,
          EMTR_EVENT_USER_LOGGED_IN, NULL /* key */,
          NULL /* auxiliary_payload */);
        g_free (object_id);
    }
    if (strcmp(SHUTDOWN_SIGNAL_NAME, signal_name) == 0) {
        gboolean before_shutdown;
        g_variant_get_child (parameters, 0, "b", &before_shutdown);
        if (before_shutdown) {
            // TODO: Inhibit shutdown until all metrics are flushed to disk.
            emtr_event_recorder_record_stop (event_recorder,
              EMTR_EVENT_USER_LOGGED_IN, NULL /* key */,
              NULL /* auxiliary_payload */);
        }
    }
}

static GDBusProxy *
login_dbus_proxy_new ()
{
    GError *error = NULL;
    GDBusProxy *dbus_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START, &LOGIN_MANAGER_INTERFACE_INFO, LOGIN_BUS_NAME,
      LOGIN_OBJECT_PATH, LOGIN_MANAGER_INTERFACE_NAME, NULL /* GCancellable */,
      &error);
    if (dbus_proxy == NULL) {
        g_printerr ("Error creating GDBusProxy: %s", error->message);
        g_error_free (error);
    } else {
        g_signal_connect (dbus_proxy, SIGNAL_NAME, G_CALLBACK (record_login),
          NULL /* data */);
    }
    return dbus_proxy;
}

int
main(int argc, const char const *argv[])
{
    GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
    event_recorder = emtr_event_recorder_new ();
    GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);
    g_main_loop_run (main_loop);
    g_main_loop_unref (main_loop);
    if (login_dbus_proxy != NULL) {
        g_object_unref (login_dbus_proxy);
    }
    g_free (event_recorder);
    return 0;
}
