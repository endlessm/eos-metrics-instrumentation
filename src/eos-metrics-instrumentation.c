/* Copyright 2014, 2015 Endless Mobile, Inc. */

/* This file is part of eos-metrics-instrumentation.
 *
 * eos-metrics-instrumentation is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * eos-metrics-instrumentation is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eos-metrics-instrumentation.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <eosmetrics/eosmetrics.h>

#include "eins-persistent-tally.h"

/*
 * Recorded when startup has finished as defined by the systemd manager DBus
 * interface. The auxiliary payload contains the parameters sent by the DBus
 * systemd manager interface as described at
 * http://www.freedesktop.org/wiki/Software/systemd/dbus/.
 */
#define STARTUP_FINISHED "bf7e8aed-2932-455c-a28e-d407cfd5aaba"

/*
 * Recorded when eos-metrics-instrumentation receives the SIGTERM signal, which
 * should generally correspond to system shutdown. The auxiliary payload
 * contains a running total of the system uptime in nanoseconds as a 64-bit
 * signed integer. This running total accumulates across boots and excludes time
 * the computer spends suspended.
 */
# define SHUTDOWN "ae391c82-1937-4ae5-8539-8d1aceed037d"

/*
 * Recorded when the system successfully pings a geolocation server to
 * retrieve the location of the system. The auxiliary payload
 * contains a string of the name of the nearest city.
 */
#define LOCATION "701c3180-8fa1-11e4-b4a9-0800200c9a66"

#define TIMEOUT_MS 10000

#define GEOLOCATION_URI "http://www.telize.com/geoip";

#define UPTIME_KEY "uptime"

/*
 * Started when a user logs in and stopped when that user logs out.
 * Payload contains the user ID of the user that logged in.
 * (Thus the payload is a GVariant containing a single unsigned 32-bit integer.)
 */
#define USER_IS_LOGGED_IN "add052be-7b2a-4959-81a5-a7f45062ee98"

#define MIN_HUMAN_USER_ID 1000

#define SHUTDOWN_INHIBITOR_UNSET -1

#define WHAT "shutdown"
#define WHO "EndlessOS Metrics Instrumentation Daemon"
#define WHY "Recording Logout/Shutdown Metrics"
#define MODE "delay"
#define INHIBIT_ARGS "('" WHAT "', '" WHO "', '" WHY "', '" MODE "')"

// Protected by humanity_by_session_id lock.
static GData *humanity_by_session_id;

G_LOCK_DEFINE_STATIC (humanity_by_session_id);

// Protected by shutdown_inhibitor lock.
static volatile gint shutdown_inhibitor = SHUTDOWN_INHIBITOR_UNSET;

G_LOCK_DEFINE_STATIC (shutdown_inhibitor);

static gboolean start_time_set = FALSE;

static gint64 start_time;

static EinsPersistentTally *persistent_tally;

/*
 * Handle a signal from the systemd manager by recording the StartupFinished
 * signal. Once the StartupFinished signal has been received, call the
 * Unsubscribe method on the systemd manager interface to stop requesting that
 * all signals be emitted.
 */
static void
record_startup (GDBusProxy *dbus_proxy,
                gchar      *sender_name,
                gchar      *signal_name,
                GVariant   *parameters,
                gpointer    user_data)
{
    if (strcmp (signal_name, "StartupFinished") == 0)
      {
        emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                          STARTUP_FINISHED, parameters);

        GError *error = NULL;
        GVariant *unsubscribe_result =
          g_dbus_proxy_call_sync (dbus_proxy, "Unsubscribe",
                                  NULL /* parameters */,
                                  G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */,
                                  NULL /* GCancellable */, &error);
        if (unsubscribe_result == NULL)
          {
            g_warning ("Error unsubscribing from systemd signals: %s.",
                       error->message);
            g_error_free (error);
            return;
          }

        g_variant_unref (unsubscribe_result);
     }
}

/* Set the global variable start_time to the current time. */
static void
set_start_time (void)
{
    start_time_set = emtr_util_get_current_time (CLOCK_MONOTONIC, &start_time);
    persistent_tally = eins_persistent_tally_new (UPTIME_KEY);

    if (!start_time_set)
      return;

    /* Cache contents of persistent tally. */
    start_time_set = eins_persistent_tally_get_tally (persistent_tally, NULL);
}

/*
 * Record a system shutdown event. Compute the length of time the system has
 * been on but not suspended using the global variable start_time. Add that time
 * to the running tally that spans across boots and report the tally as the
 * auxiliary payload of the system shutdown event.
 */
static void
record_shutdown (void)
{
    gint64 shutdown_time;
    gboolean got_shutdown_time =
      emtr_util_get_current_time (CLOCK_MONOTONIC, &shutdown_time);

    if (!got_shutdown_time || !start_time_set)
      return;

    gint64 curr_boot_uptime = shutdown_time - start_time;
    gboolean add_succeeded =
      eins_persistent_tally_add_to_tally (persistent_tally, curr_boot_uptime);

    if (!add_succeeded)
      return;

    gint64 uptime_tally;
    gboolean get_succeeded =
      eins_persistent_tally_get_tally (persistent_tally, &uptime_tally);

    if (!get_succeeded)
      return;

    GVariant *uptime_tally_variant = g_variant_new_int64 (uptime_tally);
    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      SHUTDOWN, uptime_tally_variant);

    g_object_unref (persistent_tally);
}

/*
 * Register record_startup as a signal handler for the systemd manager. Call the
 * Subscribe method on said interface to request that it emit all signals.
 */
static GDBusProxy *
systemd_dbus_proxy_new (void)
{
    GError *error = NULL;
    GDBusProxy *dbus_proxy =
      g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL /* GDBusInterfaceInfo */,
                                     "org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager",
                                     NULL /* GCancellable */, &error);
    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s.", error->message);
        g_error_free (error);
        return NULL;
      }

    g_signal_connect (dbus_proxy, "g-signal", G_CALLBACK (record_startup),
                      NULL /* data */);

    GVariant *subscribe_result =
      g_dbus_proxy_call_sync (dbus_proxy, "Subscribe", NULL /* parameters*/,
                              G_DBUS_CALL_FLAGS_NONE, -1 /* timeout */,
                              NULL /* GCancellable*/, &error);
    if (subscribe_result == NULL)
      {
        g_warning ("Error subscribing to systemd signals: %s.", error->message);
        g_error_free (error);

        /*
         * We still might receive systemd signals even though Subscribe failed.
         * As long as at least one process successfully subscribes, the systemd
         * manager will emit all signals.
         */
        return dbus_proxy;
      }

    g_variant_unref (subscribe_result);
    return dbus_proxy;
}

/*
 * Populate user_id with the user ID of the user associated with the SessionNew
 * or SessionRemoved signal with the given parameters. user_id must already be
 * allocated and non-NULL. Return TRUE if user_id was successfully populated and
 * FALSE otherwise, in which case its contents should be ignored.
 */
static gboolean
get_user_id (GVariant *session_parameters, guint32 *user_id)
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
    g_object_unref (dbus_proxy);

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
    g_variant_get_child (user_tuple, 0, "u", user_id);
    g_variant_unref (user_tuple);
    return TRUE;
}

/*
 * Return TRUE if the given parameters of a SessionNew or SessionRemoved signal
 * correspond to a human session. If the given parameters don't correspond to a
 * human session, or this can't be determined, return FALSE.
 */
static gboolean
is_human_session (GVariant *session_parameters)
{
    guint32 user_id;
    if (!get_user_id (session_parameters, &user_id))
      return FALSE;
    return user_id >= MIN_HUMAN_USER_ID;
}

/*
 * If the given parameters of a SessionNew or SessionRemoved signal correspond
 * to a human session, return TRUE and add the corresponding session_id to the
 * humanity_by_session_id set. Otherwise, return FALSE.
 */
static gboolean
add_session_to_set (GVariant *session_parameters)
{
    if (!is_human_session (session_parameters))
      return FALSE;

    gchar *session_id;
    g_variant_get_child (session_parameters, 0, "s", &session_id);
    G_LOCK (humanity_by_session_id);
    g_datalist_set_data (&humanity_by_session_id, session_id, (gpointer) TRUE);
    G_UNLOCK (humanity_by_session_id);
    g_free (session_id);
    return TRUE;
}

/*
 * If the given parameters of a SessionNew or SessionRemoved signal correspond
 * to a human session that is currently in the humanity_by_session_id set,
 * return TRUE and remove the session from the set. Otherwise, return FALSE.
 */
static gboolean
remove_session_from_set (GVariant *session_parameters)
{
    gchar *session_id;
    g_variant_get_child (session_parameters, 0, "s", &session_id);

    G_LOCK (humanity_by_session_id);
    gboolean is_human = (gboolean) g_datalist_get_data (&humanity_by_session_id,
                                                        session_id);

    if (is_human)
      g_datalist_remove_data (&humanity_by_session_id, session_id);

    G_UNLOCK (humanity_by_session_id);

    g_free (session_id);
    return is_human;
}

/*
 * Intended for use as a GDataForeachFunc callback. Records a logout for the
 * given session ID.
 */
static void
record_stop_for_login (GQuark   session_id_quark,
                       gpointer unused,
                       gpointer user_data)
{
    const gchar *session_id = g_quark_to_string (session_id_quark);
    GVariant *session_id_variant = g_variant_new_string (session_id);
    emtr_event_recorder_record_stop (emtr_event_recorder_get_default (),
                                     USER_IS_LOGGED_IN,
                                     session_id_variant,
                                     NULL /* auxiliary_payload */);
}

/*
 * Inhibit shutdown if we don't already hold a valid shutdown inhibitor.
 */
static void
inhibit_shutdown (GDBusProxy *dbus_proxy)
{
    G_LOCK (shutdown_inhibitor);
    if (shutdown_inhibitor == SHUTDOWN_INHIBITOR_UNSET)
      {
        GVariant *inhibit_args = g_variant_new_parsed (INHIBIT_ARGS);
        GError *error = NULL;
        GUnixFDList *fd_list = NULL;
        GVariant *inhibitor_tuple =
          g_dbus_proxy_call_with_unix_fd_list_sync (dbus_proxy, "Inhibit",
                                                    inhibit_args,
                                                    G_DBUS_CALL_FLAGS_NONE,
                                                    -1 /* timeout */,
                                                    NULL /* input fd_list */,
                                                    &fd_list /* out fd_list */,
                                                    NULL /* GCancellable */,
                                                    &error);
        if (inhibitor_tuple == NULL)
          {
            if (fd_list != NULL)
              g_object_unref (fd_list);
            g_warning ("Error inhibiting shutdown: %s\n", error->message);
            g_error_free (error);
            goto finally;
          }

        g_variant_unref (inhibitor_tuple);

        gint fd_list_length;
        gint *fds = g_unix_fd_list_steal_fds (fd_list, &fd_list_length);
        g_object_unref (fd_list);
        if (fd_list_length != 1)
          {
            g_warning ("Error inhibiting shutdown. Login manager returned %d "
                       "file descriptors, but we expected 1 file descriptor.",
                       fd_list_length);
            g_free (fds);
            goto finally;
          }
        shutdown_inhibitor = fds[0];
        g_free (fds);
      }

finally:
    G_UNLOCK (shutdown_inhibitor);
}

/*
 * Stop inhibiting shutdown unless we don't hold a shutdown inhibitor in the
 * first place.
 */
static void
stop_inhibiting_shutdown (void)
{
    G_LOCK (shutdown_inhibitor);

    if (shutdown_inhibitor != SHUTDOWN_INHIBITOR_UNSET)
      {
        gint volatile previous_shutdown_inhibitor = shutdown_inhibitor;
        shutdown_inhibitor = SHUTDOWN_INHIBITOR_UNSET;
        G_UNLOCK (shutdown_inhibitor);

        GError *error = NULL;

        // If the system is shutting down, this daemon may be killed at any
        // point after this statement.
        if (!g_close (previous_shutdown_inhibitor, &error))
          {
            g_warning ("Failed to release shutdown inhibitor. Error: %s.",
                       error->message);
            g_error_free (error);
          }
      }
    else
      {
        G_UNLOCK (shutdown_inhibitor);
      }
}

/*
 * Handle a signal from the login manager by recording login/logout pairs. Make
 * the aggressive assumption that all sessions end when the PrepareForShutdown
 * signal is sent with parameter TRUE. This isn't necessarily a valid assumption
 * because the shutdown can be cancelled, but in practice we don't get the
 * SessionRemoved signal in time if the user shuts down without first logging
 * out.
 *
 * Recording of logins must be 1:1 with recording of logouts, so each time
 * we record a login we add the session ID to the humanity_by_session_id set,
 * and each time we record a logout we remove the session ID from the
 * humanity_by_session_id set.
 */
static void
record_login (GDBusProxy *dbus_proxy,
              gchar      *sender_name,
              gchar      *signal_name,
              GVariant   *parameters,
              gpointer    user_data)
{
    if (strcmp ("PrepareForShutdown", signal_name) == 0)
      {
        gboolean shutting_down;
        g_variant_get_child (parameters, 0, "b", &shutting_down);
        if (shutting_down)
          {
            G_LOCK (humanity_by_session_id);
            g_datalist_foreach (&humanity_by_session_id,
                                (GDataForeachFunc) record_stop_for_login,
                                NULL /* user_data */);
            g_datalist_clear (&humanity_by_session_id);
            G_UNLOCK (humanity_by_session_id);
            stop_inhibiting_shutdown ();
          }
        else
          {
            inhibit_shutdown (dbus_proxy);
          }
      }
    else if (strcmp ("SessionRemoved", signal_name) == 0 &&
             remove_session_from_set (parameters))
      {
        GVariant *session_id = g_variant_get_child_value (parameters, 0);
        emtr_event_recorder_record_stop (emtr_event_recorder_get_default (),
                                         USER_IS_LOGGED_IN, session_id,
                                         NULL /* auxiliary_payload */);
        g_variant_unref (session_id);
      }
    else if (strcmp ("SessionNew", signal_name) == 0 &&
             add_session_to_set (parameters))
      {
        inhibit_shutdown (dbus_proxy);
        GVariant *session_id = g_variant_get_child_value (parameters, 0);

        guint32 user_id;
        gboolean user_id_is_valid = get_user_id (parameters, &user_id);
        GVariant *user_id_variant = user_id_is_valid ?
          g_variant_new_uint32 (user_id) : NULL;

        emtr_event_recorder_record_start (emtr_event_recorder_get_default (),
                                          USER_IS_LOGGED_IN, session_id,
                                          user_id_variant);
        g_variant_unref (session_id);
      }
}

static GDBusProxy *
login_dbus_proxy_new (void)
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
        g_variant_get (parameters, "(u)", &new_network_state);

        G_LOCK (previous_network_state);

        GVariant *status_change = g_variant_new ("(uu)", previous_network_state,
                                                 new_network_state);

        emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                          EMTR_EVENT_NETWORK_STATUS_CHANGED,
                                          status_change);

        previous_network_state = new_network_state;

        G_UNLOCK (previous_network_state);
      }
}

static GDBusProxy *
network_dbus_proxy_new (void)
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

static gboolean
quit_main_loop (GMainLoop *main_loop)
{
    g_main_loop_quit (main_loop);
    return G_SOURCE_REMOVE;
}

/*
 * Returns a string of the user's city. This string is parsed from
 * the contents of the received JSON response. The string manipulation
 * in this function is specific to the geolocation service and should
 * change if the service does.
 */
static gchar *
get_city_from_contents(gchar *contents)
{
    gchar *start = (gchar *)(g_strrstr(contents, "city") + 7);
    gchar *end = (gchar *)(g_strrstr(contents, "timezone") - 3);
    return g_strndup(start, end - start);
}


static void
geo_cb(GFile *gfile,
       GAsyncResult *res,
       gpointer user_data)
{
    gchar *contents;
    gsize length;
    GError *error = NULL;

    g_file_load_contents_finish(gfile, res, &contents, &length, NULL, &error);

    if (error != NULL) {
        g_warning ("Error unsubscribing from systemd signals: %s.",
                   error->message);
        g_error_free (error);
        return;
    }

    gchar *city = get_city_from_contents(contents);
    GVariant *location_variant = g_variant_new_string(city);

    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      LOCATION,
                                      location_variant);
    g_free(city);
    g_variant_unref(location_variant);
}

static gboolean
check_geo_request(GCancellable *geo_cancellable)
{
    if (!g_cancellable_is_cancelled(geo_cancellable)) {
        g_cancellable_cancel(geo_cancellable);
    }

    return G_SOURCE_REMOVE;
}

static void
record_location(GMainLoop *loop)
{
    GCancellable *geo_cancellable = g_cancellable_new();

    g_timeout_add(TIMEOUT_MS, (GSourceFunc)check_geo_request, geo_cancellable);

    GFile *gfile = g_file_new_for_uri(GEOLOCATION_URI);
    g_file_load_contents_async(gfile, geo_cancellable, (GAsyncReadyCallback)geo_cb, NULL);
}

int
main(int                argc,
     const char * const argv[])
{
    set_start_time ();
    g_datalist_init (&humanity_by_session_id);
    GDBusProxy *systemd_dbus_proxy = systemd_dbus_proxy_new ();
    GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
    GDBusProxy *network_dbus_proxy = network_dbus_proxy_new ();
    GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);
    record_location (main_loop);
    g_unix_signal_add (SIGHUP, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGINT, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGTERM, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR1, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR2, (GSourceFunc) quit_main_loop, main_loop);
    g_main_loop_run (main_loop);

    record_shutdown ();
    g_main_loop_unref (main_loop);
    g_clear_object (&systemd_dbus_proxy);
    g_clear_object (&login_dbus_proxy);
    g_clear_object (&network_dbus_proxy);
    G_LOCK (humanity_by_session_id);
    g_datalist_clear (&humanity_by_session_id);
    G_UNLOCK (humanity_by_session_id);
    stop_inhibiting_shutdown ();
    return EXIT_SUCCESS;
}
