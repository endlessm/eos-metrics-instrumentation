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
#include <glib-object.h>
#include <glib-unix.h>
#include <string.h>

#include <eosmetrics/eosmetrics.h>

#include "eins-hwinfo.h"

/*
 * Recorded when startup has finished as defined by the systemd manager DBus
 * interface. The auxiliary payload contains the parameters sent by the DBus
 * systemd manager interface as described at
 * http://www.freedesktop.org/wiki/Software/systemd/dbus/.
 */
#define STARTUP_FINISHED "bf7e8aed-2932-455c-a28e-d407cfd5aaba"

#define BOOT_COUNT_KEY "boot_count"

/*
 * The event ID to record user session's alive time from login to logout.
 * https://azafea.readthedocs.io/en/latest/events.html#azafea.event_processors.endless.metrics.v3.model.DailySessionTime
 */
#define DAILY_SESSION_TIME "5dc0b53c-93f9-4df0-ad6f-bd25e9fe638f"

#define MIN_HUMAN_USER_ID 1000

/*
 * Map from user ID of logged-in user (guint32) to EmtrAggregateTimer (owned
 * by hash table).
 */
static GHashTable *session_by_user_id;

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
                                   NULL /* GCancellable */,
                                   &error);
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

static gpointer
userid_to_key (guint32 user_id)
{
  return GUINT_TO_POINTER (user_id);
}

/*
 * Create a new session for the user, then start the corresponding aggregate
 * timer.
 */
static void
add_session (guint32 user_id)
{
  EmtrAggregateTimer *timer = NULL;

  /*  Only care about real humans */
  if (user_id < MIN_HUMAN_USER_ID)
    return;

  timer = emtr_event_recorder_start_aggregate_timer (emtr_event_recorder_get_default (),
                                                     DAILY_SESSION_TIME,
                                                     g_variant_new_uint32 (user_id),
                                                     NULL);
  if (timer == NULL)
    {
      g_warning ("Failed to start an aggregate timer for user %u", user_id);
    }
  else {
    g_hash_table_insert (session_by_user_id, userid_to_key (user_id), timer);
  }
}

/*
 * Stop the corresponding aggregate timer, then remove the session.
 */
static void
remove_session (guint32 user_id)
{
  /* The timer will stop itself when its reference count falls to 0. */
  g_hash_table_remove (session_by_user_id, userid_to_key (user_id));
}

/*
 * Handle the pair of UserNew and UserRemoved signals from the login manager,
 * emitted when the user's first concurrent session begins and last concurrent
 * session ends (respectively), to record cumulative session time for each
 * user.
 *
 * The login manager guarantees that these signals are emitted in pairs,
 * so we start a timer and store it in a hash table on UserNew, and stop &
 * remove it on UserRemoved.
 *
 * TODO: Pause timers when users' sessions are all idle, such as when the
 * screen is locked or another user is actively using the system.
 */
static void
record_login (GDBusProxy *dbus_proxy,
              gchar      *sender_name,
              gchar      *signal_name,
              GVariant   *parameters,
              gpointer    user_data)
{
  guint32 user_id;

  if (strcmp ("UserNew", signal_name) == 0)
    {
      g_variant_get (parameters, "(u&o)", &user_id, NULL);
      add_session (user_id);
    }
  else if (strcmp ("UserRemoved", signal_name) == 0)
    {
      g_variant_get (parameters, "(u&o)", &user_id, NULL);
      remove_session (user_id);
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
                                   NULL /* GCancellable */,
                                   &error);
  if (error)
    {
      g_warning ("Error creating GDBusProxy: %s.", error->message);
      g_error_free (error);
      return NULL;
    }

  g_signal_connect (dbus_proxy, "g-signal", G_CALLBACK (record_login),
                    NULL /* data */);

  GVariant *users =
    g_dbus_proxy_call_sync (dbus_proxy, "ListUsers", NULL,
                            G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error)
    {
      g_warning ("Error calling ListUsers: %s.", error->message);
      g_error_free (error);
      return NULL;
    }

  g_autoptr(GVariantIter) user_iter = NULL;
  g_variant_get (users, "(a(uso))", &user_iter);

  guint32 user_id;
  while (g_variant_iter_loop (user_iter, "(us&o)", &user_id, NULL, NULL))
    {
      add_session (user_id);
    }

  return dbus_proxy;
}

static gboolean
quit_main_loop (GMainLoop *main_loop)
{
  g_main_loop_quit (main_loop);
  return G_SOURCE_REMOVE;
}

gint
main (gint                argc,
      const gchar * const argv[])
{
  session_by_user_id = g_hash_table_new (g_direct_hash, g_direct_equal);

  GDBusProxy *systemd_dbus_proxy = systemd_dbus_proxy_new ();
  GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();

  GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);

  eins_hwinfo_start ();

  g_unix_signal_add (SIGHUP, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGINT, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGTERM, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGUSR1, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGUSR2, (GSourceFunc) quit_main_loop, main_loop);

  g_main_loop_run (main_loop);

  /*
   * Remove all remained login records for killing this daemon, for example
   * poweroff system.
   */
  g_hash_table_remove_all (session_by_user_id);
  g_hash_table_unref (session_by_user_id);

  g_main_loop_unref (main_loop);
  g_clear_object (&systemd_dbus_proxy);
  g_clear_object (&login_dbus_proxy);

  return EXIT_SUCCESS;
}
