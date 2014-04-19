/* Copyright 2014 Endless Mobile, Inc. */

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <eosmetrics/eosmetrics.h>

#define MIN_HUMAN_USER_ID 1000

#define WHAT "shutdown"
#define WHO "EndlessOS Metrics Instrumentation Daemon"
#define WHY "Recording Logout/Shutdown Metrics"
#define MODE "delay"
#define INHIBIT_ARGS "('" WHAT "', '" WHO "', '" WHY "', '" MODE "')"

static EmtrEventRecorder *event_recorder;

// Protected by humanity_by_session_id lock.
static GData *humanity_by_session_id;

G_LOCK_DEFINE_STATIC (humanity_by_session_id);

// Protected by shutdown_inhibitor lock.
static volatile FILE * volatile shutdown_inhibitor = NULL;

// Protected by shutdown_inhibitor lock.
static volatile gboolean should_inhibit_shutdown = TRUE;

G_LOCK_DEFINE_STATIC (shutdown_inhibitor);

static gboolean
is_human_session (GVariant *session_parameters)
{
    GError *error = NULL;
    gchar *session_path;
    g_variant_get_child (session_parameters, 1, "o", &session_path);
    GDBusProxy *dbus_proxy =
      g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL /* GDBusInterfaceInfo */,
                                     "org.freedesktop.login1",
                                     session_path,
                                     "org.freedesktop.DBus.Properties",
                                     NULL /* GCancellable */, &error);
    g_free (session_path);

    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s\n", error->message);
        g_error_free (error);
        return FALSE;
      }

    GVariant *get_user_args =
      g_variant_new_parsed ("('org.freedesktop.login1.Session', 'User')");
    GVariant *user_result = g_dbus_proxy_call_sync (dbus_proxy, "Get",
                                                    get_user_args,
                                                    G_DBUS_CALL_FLAGS_NONE,
                                                    -1 /* timeout */,
                                                    NULL /* GCancellable */,
                                                    &error);
 
    if (user_result == NULL)
      {
        g_warning ("Error getting user ID: %s\n", error->message);
        g_error_free (error);
        return FALSE;
      }

    GVariant *user_variant = g_variant_get_child_value (user_result, 0);
    g_variant_unref (user_result);
    GVariant *user_tuple = g_variant_get_child_value (user_variant, 0);
    g_variant_unref (user_variant);
    guint32 user_id;
    g_variant_get_child (user_tuple, 0, "u", &user_id);
    g_variant_unref (user_tuple);
    return user_id >= MIN_HUMAN_USER_ID;
}

static gboolean
set_is_human_session (GVariant *session_parameters)
{
    gboolean *ptr_to_humanity = g_new (gboolean, 1);
    *ptr_to_humanity = is_human_session (session_parameters);
    gchar *session_id;
    g_variant_get_child (session_parameters, 0, "s", &session_id);
    G_LOCK (humanity_by_session_id);
    g_datalist_set_data_full (&humanity_by_session_id, session_id,
                              ptr_to_humanity, g_free);
    G_UNLOCK (humanity_by_session_id);
    g_free (session_id);
    return *ptr_to_humanity;
}

static gboolean
remove_is_human_session (GVariant *session_parameters)
{
    gchar *session_id;
    g_variant_get_child (session_parameters, 0, "s", &session_id);
    G_LOCK (humanity_by_session_id);
    gboolean *ptr_to_humanity = g_datalist_get_data (&humanity_by_session_id,
                                                     session_id);
    gboolean is_human = FALSE;
    if (ptr_to_humanity != NULL)
      {
        is_human = *ptr_to_humanity;
        g_datalist_remove_data (&humanity_by_session_id, session_id);
      }
    G_UNLOCK (humanity_by_session_id);
    g_free (session_id);
    return is_human;
}

static void
maybe_inhibit_shutdown (GDBusProxy *dbus_proxy)
{
    G_LOCK (shutdown_inhibitor);
    if (should_inhibit_shutdown)
      {
        GVariant *inhibit_args = g_variant_new_parsed (INHIBIT_ARGS);
        GError *error = NULL;
        GVariant *inhibitor_tuple =
          g_dbus_proxy_call_sync (dbus_proxy, "Inhibit", inhibit_args,
                                  G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */,
                                  NULL /* GCancellable */, &error);
        if (inhibitor_tuple == NULL)
          {
            g_warning ("Error inhibiting shutdown: %s\n", error->message);
            g_error_free (error);
          }
        else
          {
            g_variant_get_child (inhibitor_tuple, 0, "h", &shutdown_inhibitor);
            g_variant_unref (inhibitor_tuple);
            // There is no value in inhibiting shutdown twice in one boot.
            should_inhibit_shutdown = FALSE;
          }
      }
    G_UNLOCK (shutdown_inhibitor);
}

static void
stop_inhibiting_shutdown ()
{
    G_LOCK (shutdown_inhibitor);

    // We are done recording metrics.
    should_inhibit_shutdown = FALSE;

    if (shutdown_inhibitor != NULL)
      {
        FILE * volatile previous_shutdown_inhibitor =
          (FILE * volatile) shutdown_inhibitor;
        shutdown_inhibitor = NULL;
        G_UNLOCK (shutdown_inhibitor);

        // If the system is shutting down, this daemon may be killed at any
        // point after this statement.
        fclose (previous_shutdown_inhibitor);
      }
    else
      {
        G_UNLOCK (shutdown_inhibitor);
      }
}

static void
record_login (GDBusProxy *dbus_proxy,
              gchar      *sender_name,
              gchar      *signal_name,
              GVariant   *parameters,
              gpointer    user_data)
{
    if (strcmp ("SessionNew", signal_name) == 0 &&
        set_is_human_session (parameters))
      {
        maybe_inhibit_shutdown (dbus_proxy);
        GVariant *session_id = g_variant_get_child_value (parameters, 0);
        emtr_event_recorder_record_start (event_recorder,
                                          EMTR_EVENT_USER_LOGGED_IN, session_id,
                                          NULL /* auxiliary_payload */);
        g_variant_unref (session_id);
      }
    else if (strcmp ("SessionRemoved", signal_name) == 0 &&
             remove_is_human_session (parameters))
      {
        GVariant *session_id = g_variant_get_child_value (parameters, 0);
        emtr_event_recorder_record_stop (event_recorder,
                                         EMTR_EVENT_USER_LOGGED_IN, session_id,
                                         NULL /* auxiliary_payload */);
        g_variant_unref (session_id);
        stop_inhibiting_shutdown ();
      }
    else if (strcmp ("PrepareForShutdown", signal_name) == 0)
      {
        gboolean before_shutdown;
        g_variant_get_child (parameters, 0, "b", &before_shutdown);
        if (before_shutdown)
          {
            G_LOCK (shutdown_inhibitor);
            // It is an error to inhibit shutdown after the PrepareForShutdown
            // signal has been sent with its parameter set to TRUE.
            should_inhibit_shutdown = FALSE;
            G_UNLOCK (shutdown_inhibitor);
          }
      }
}

static GDBusProxy *
login_dbus_proxy_new ()
{
    GError *error = NULL;
    GDBusProxy *dbus_proxy =
      g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL /* GDBusInterfaceInfo */,
                                     "org.freedesktop.login1",
                                     "/org/freedesktop/login1",
                                     "org.freedesktop.login1.Manager",
                                     NULL /* GCancellable */, &error);
    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s\n", error->message);
        g_error_free (error);
      }
    else
      {
        g_signal_connect (dbus_proxy, "g-signal", G_CALLBACK (record_login),
                          NULL /* data */);
      }
    return dbus_proxy;
}

static volatile guint32 previous_network_state = 0; // NM_STATE_UNKNOWM

G_LOCK_DEFINE_STATIC (previous_network_state);

static void
record_network_change (GDBusProxy *dbus_proxy,
                       gchar      *sender_name,
                       gchar      *signal_name,
                       GVariant   *parameters,
                       gpointer    user_data)
{
    if (strcmp ("StateChanged", signal_name) == 0)
      {
        guint32 new_network_state;
        g_variant_get(parameters, "(u)", &new_network_state);

        G_LOCK (previous_network_state);

        GVariant *status_change = g_variant_new("(uu)", previous_network_state,
                                                new_network_state);

        emtr_event_recorder_record_event (event_recorder,
                                          EMTR_EVENT_NETWORK_STATUS_CHANGED,
                                          status_change);

        previous_network_state = new_network_state;

        G_UNLOCK (previous_network_state);
      }
}

static GDBusProxy *
network_dbus_proxy_new ()
{
    GError *error = NULL;
    GDBusProxy *dbus_proxy =
      g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL /* GDBusInterfaceInfo */,
                                     "org.freedesktop.NetworkManager",
                                     "/org/freedesktop/NetworkManager",
                                     "org.freedesktop.NetworkManager",
                                     NULL /* GCancellable */, &error);
    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s\n", error->message);
        g_error_free (error);
      }
    else
      {
        g_signal_connect (dbus_proxy, "g-signal", G_CALLBACK (record_network_change),
                          NULL /* data */);
      }
    return dbus_proxy;
}

static void
record_social_bar_change (GDBusProxy *dbus_proxy,
                          gchar      *sender_name,
                          gchar      *signal_name,
                          GVariant   *parameters,
                          gpointer    user_data)
  {
    if (strcmp ("PropertiesChanged", signal_name) == 0)
      {
        GVariant *propeties_changed_dictionary = g_variant_get_child_value (parameters, 1);
        GVariant *visibility_wrapper = g_variant_lookup_value (propeties_changed_dictionary, 
                                                               "Visible", 
                                                               G_VARIANT_TYPE_BOOLEAN);
        gboolean is_visible = g_variant_get_boolean (visibility_wrapper);
        
        if (is_visible)
            emtr_event_recorder_record_start (event_recorder,
                                              EMTR_EVENT_SOCIAL_BAR_VISIBLE,
                                              NULL,
                                              NULL);
        else
            emtr_event_recorder_record_stop (event_recorder,
                                             EMTR_EVENT_SOCIAL_BAR_VISIBLE,
                                             NULL,
                                             NULL);
      }
}

static GDBusProxy *
social_bar_dbus_proxy_new ()
{
    GError *error = NULL;
    GDBusProxy *dbus_proxy =
      g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL /* GDBusInterfaceInfo */,
                                     "com.endlessm.SocialBar",
                                     "/com/endlessm/SocialBar",
                                     "org.freedesktop.DBus.Properties",
                                     NULL /* GCancellable */, &error);
    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s\n", error->message);
        g_error_free (error);
      }
    else
      {
        g_signal_connect (dbus_proxy, "g-signal", G_CALLBACK (record_social_bar_change),
                          NULL /* data */);
      }
    return dbus_proxy;
}

static gboolean
quit_main_loop (gpointer user_data)
{
    GMainLoop *main_loop = (GMainLoop *) user_data;
    g_main_loop_quit (main_loop);
    return G_SOURCE_REMOVE;
}

int
main(int                argc,
     const char * const argv[])
{
    event_recorder = emtr_event_recorder_new ();
    g_datalist_init (&humanity_by_session_id);
    GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
    GDBusProxy *network_dbus_proxy = network_dbus_proxy_new ();
    GDBusProxy *social_bar_dbus_proxy = social_bar_dbus_proxy_new ();
    GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);
    g_unix_signal_add (SIGHUP, quit_main_loop, main_loop);
    g_unix_signal_add (SIGINT, quit_main_loop, main_loop);
    g_unix_signal_add (SIGTERM, quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR1, quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR2, quit_main_loop, main_loop);
    g_main_loop_run (main_loop);

    g_main_loop_unref (main_loop);
    g_clear_object (&login_dbus_proxy);
    g_clear_object (&network_dbus_proxy);
    g_clear_object (&social_bar_dbus_proxy);
    G_LOCK (humanity_by_session_id);
    g_datalist_clear (&humanity_by_session_id);
    G_UNLOCK (humanity_by_session_id);
    g_object_unref (event_recorder);
    stop_inhibiting_shutdown ();
    return EXIT_SUCCESS;
}
