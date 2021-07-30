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
#include "eins-location-label.h"
#include "eins-persistent-tally.h"

/*
 * Recorded when startup has finished as defined by the systemd manager DBus
 * interface. The auxiliary payload contains the parameters sent by the DBus
 * systemd manager interface as described at
 * http://www.freedesktop.org/wiki/Software/systemd/dbus/.
 */
#define STARTUP_FINISHED "bf7e8aed-2932-455c-a28e-d407cfd5aaba"

#define BOOT_COUNT_KEY "boot_count"

/*
 * Started when a user logs in and stopped when that user logs out.
 * Payload contains the user ID of the user that logged in.
 * (Thus the payload is a GVariant containing a single unsigned 32-bit integer.)
 */
#define USER_IS_LOGGED_IN "add052be-7b2a-4959-81a5-a7f45062ee98"

#define MIN_HUMAN_USER_ID 1000

/* Recorded at every startup to track deployment statistics. The auxiliary
 * payload is a 3-tuple of the form (os_name, os_version, eos_personality).
 * From 3.2.0 the personality is always reported as "" because the image
 * version event can be used.
 */
#define OS_VERSION_EVENT "1fa16a31-9225-467e-8502-e31806e9b4eb"

#define OS_RELEASE_FILE "/etc/os-release"

/*
 * Recorded once at startup when booted from a combined live + installer USB
 * stick. We expect metrics reported from live sessions to be different to those
 * from installed versions of the OS, not least because live sessions are
 * transient, so each boot will appear to be a new installation, booted for the
 * first time. There is no payload.
 */
#define LIVE_BOOT_EVENT "56be0b38-e47b-4578-9599-00ff9bda54bb"

/*
 * Recorded once at startup on dual-boot installations. This is
 * mutually-exclusive with LIVE_BOOT_EVENT. There is no payload.
 */
#define DUAL_BOOT_EVENT "16cfc671-5525-4a99-9eb9-4f6c074803a9"

#define KERNEL_CMDLINE_PATH "/proc/cmdline"
#define LIVE_BOOT_FLAG_REGEX "\\bendless\\.live_boot\\b"
#define DUAL_BOOT_FLAG_REGEX "\\bendless\\.image\\.device\\b"

static EinsPersistentTally *persistent_tally;

static GData *humanity_by_session_id;

static gboolean
record_os_version (gpointer unused)
{
  g_autofree gchar *os_name = g_get_os_info (G_OS_INFO_KEY_NAME);
  g_autofree gchar *os_version = g_get_os_info (G_OS_INFO_KEY_VERSION);

  if (os_name == NULL || os_version == NULL)
    {
      g_warning ("%s: Could not find at least one of NAME or VERSION",
                 G_STRFUNC);
      return G_SOURCE_REMOVE;
    }

  GVariant *payload = g_variant_new ("(sss)",
                                     os_name, os_version, "");
  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    OS_VERSION_EVENT, payload);

  return G_SOURCE_REMOVE;
}

static void
check_cmdline (gboolean *is_live_boot,
               gboolean *is_dual_boot)
{
  g_autofree gchar *cmdline = NULL;
  g_autoptr(GError) error = NULL;

  *is_live_boot = FALSE;
  *is_dual_boot = FALSE;

  if (!g_file_get_contents (KERNEL_CMDLINE_PATH, &cmdline, NULL, &error))
    {
      g_warning ("Error reading " KERNEL_CMDLINE_PATH ": %s", error->message);
    }
  else if (g_regex_match_simple (LIVE_BOOT_FLAG_REGEX, cmdline, 0, 0))
    {
      *is_live_boot = TRUE;
    }
  else if (g_regex_match_simple (DUAL_BOOT_FLAG_REGEX, cmdline, 0, 0))
    {
      *is_dual_boot = TRUE;
    }
}

static gboolean
record_live_boot (gpointer unused)
{
  gboolean is_live_boot, is_dual_boot;

  check_cmdline (&is_live_boot, &is_dual_boot);

  if (is_live_boot)
    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      LIVE_BOOT_EVENT, NULL);
  else if (is_dual_boot)
    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      DUAL_BOOT_EVENT, NULL);

  return G_SOURCE_REMOVE;
}

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

static gboolean
increment_boot_count (gpointer unused)
{
  const gchar *tally_file_override = g_getenv ("EOS_INSTRUMENTATION_CACHE");
  GError *error = NULL;
  if (tally_file_override != NULL)
    persistent_tally =
      eins_persistent_tally_new_full (tally_file_override, &error);
  else
    persistent_tally = eins_persistent_tally_new (&error);

  if (persistent_tally == NULL)
    {
      g_warning ("Could not create persistent tally object: %s.",
                 error->message);
      g_error_free (error);
      return G_SOURCE_REMOVE;
    }

  eins_persistent_tally_add_to_tally (persistent_tally, BOOT_COUNT_KEY, 1);
  return G_SOURCE_REMOVE;
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

/*
 * Populate user_id with the user ID of the user associated with the given
 * logind session object at session_path. user_id must already be allocated
 * and non-NULL. Return TRUE if user_id was successfully populated and
 * FALSE otherwise, in which case its contents should be ignored.
 */
static gboolean
get_user_id (const gchar *session_path,
             guint32     *user_id)
{
  GError *error = NULL;
  GDBusProxy *dbus_proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_NONE,
                                   NULL /* GDBusInterfaceInfo */,
                                   "org.freedesktop.login1",
                                   session_path,
                                   "org.freedesktop.DBus.Properties",
                                   NULL /* GCancellable */,
                                   &error);

  if (dbus_proxy == NULL)
    {
      g_warning ("Error creating GDBusProxy: %s.", error->message);
      g_error_free (error);
      return FALSE;
    }

  GVariant *get_user_args =
    g_variant_new_parsed ("('org.freedesktop.login1.Session', 'User')");
  GVariant *user_result = g_dbus_proxy_call_sync (dbus_proxy,
                                                  "Get",
                                                  get_user_args,
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1 /* timeout */,
                                                  NULL /* GCancellable */,
                                                  &error);
  g_object_unref (dbus_proxy);

  if (user_result == NULL)
    {
      g_warning ("Error getting user ID: %s.", error->message);
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

static gboolean
is_human_session (const gchar *session_id)
{
  /* All normal user sessions start with digits -- greeter sessions
   * start with 'c'.
   */
  return g_ascii_isdigit (session_id[0]);
}

static gboolean
session_in_set (const gchar *session_id)
{
  gpointer data = g_datalist_get_data (&humanity_by_session_id, session_id);
  return (gboolean) GPOINTER_TO_UINT (data);
}

/*
 * If the given session_id corresponds to a human session not already in the
 * set, return TRUE and add the corresponding session_id to the
 * humanity_by_session_id set. Otherwise, return FALSE.
 */
static gboolean
add_session_to_set (const gchar *session_id)
{
  if (!is_human_session (session_id) || session_in_set (session_id))
    return FALSE;

  g_datalist_set_data (&humanity_by_session_id, session_id,
                       GUINT_TO_POINTER (TRUE));
  return TRUE;
}

/*
 * If the given session_id corresponds to a human session tracked inside the
 * humanity_by_session_id set, remove it and return TRUE. Otherwise, return
 * FALSE.
 */
static gboolean
remove_session_from_set (const gchar *session_id)
{
  if (!is_human_session (session_id) || !session_in_set (session_id))
    return FALSE;

  g_datalist_remove_data (&humanity_by_session_id, session_id);
  return TRUE;
}

/*
 * Intended for use as a GDataForeachFunc callback. Synchronously records a
 * logout for the given session ID.
 */
static void
record_stop_for_login (GQuark   session_id_quark,
                       gpointer unused,
                       gpointer user_data)
{
  const gchar *session_id = g_quark_to_string (session_id_quark);
  GVariant *session_id_variant = g_variant_new_string (session_id);
  emtr_event_recorder_record_stop_sync (emtr_event_recorder_get_default (),
                                        USER_IS_LOGGED_IN,
                                        session_id_variant,
                                        NULL /* auxiliary_payload */);
}

static void
add_session (const gchar *session_id,
             guint32      user_id)
{
  if (!add_session_to_set (session_id))
    return;

  GVariant *user_id_variant =
    (user_id >= MIN_HUMAN_USER_ID) ? g_variant_new_uint32 (user_id) : NULL;

  emtr_event_recorder_record_start (emtr_event_recorder_get_default (),
                                    USER_IS_LOGGED_IN,
                                    g_variant_new_string (session_id),
                                    user_id_variant);
}

static void
remove_session (const gchar *session_id)
{
  if (!remove_session_from_set (session_id))
    return;

  emtr_event_recorder_record_stop_sync (emtr_event_recorder_get_default (),
                                        USER_IS_LOGGED_IN,
                                        g_variant_new_string (session_id),
                                        NULL /* auxiliary_payload */);
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
  if (strcmp ("SessionRemoved", signal_name) == 0)
    {
      const gchar *session_id;
      g_variant_get (parameters, "(&s&o)", &session_id, NULL);

      remove_session (session_id);
    }
  else if (strcmp ("SessionNew", signal_name) == 0)
    {
      const gchar *session_id, *session_path;
      g_variant_get (parameters, "(&s&o)", &session_id, &session_path);

      guint32 user_id;
      if (!get_user_id (session_path, &user_id))
        return;

      add_session (session_id, user_id);
    }
}

static void
record_logout_for_all_remaining_sessions (void)
{
  g_datalist_foreach (&humanity_by_session_id,
                      (GDataForeachFunc) record_stop_for_login,
                      NULL /* user_data */);
  g_datalist_clear (&humanity_by_session_id);
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

  GVariant *sessions =
    g_dbus_proxy_call_sync (dbus_proxy, "ListSessions", NULL,
                            G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error)
    {
      g_warning ("Error calling ListSessions: %s.", error->message);
      g_error_free (error);
      return NULL;
    }

  GVariantIter *session_iter;
  g_variant_get (sessions, "(a(susso))", &session_iter);

  const gchar *session_id;
  guint32 user_id;
  while (g_variant_iter_loop (session_iter, "(&suss&o)", &session_id,
                              &user_id, NULL, NULL, NULL))
    {
      add_session (session_id, user_id);
    }

  g_variant_iter_free (session_iter);
  g_variant_unref (sessions);

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
  g_datalist_init (&humanity_by_session_id);

  GDBusProxy *systemd_dbus_proxy = systemd_dbus_proxy_new ();
  GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
  GFileMonitor *location_file_monitor = location_file_monitor_new ();

  GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);

  g_idle_add ((GSourceFunc) record_os_version, NULL);
  g_idle_add ((GSourceFunc) increment_boot_count, NULL);
  g_idle_add ((GSourceFunc) record_live_boot, NULL);
  g_idle_add ((GSourceFunc) record_location_label, NULL);

  eins_hwinfo_start ();

  g_unix_signal_add (SIGHUP, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGINT, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGTERM, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGUSR1, (GSourceFunc) quit_main_loop, main_loop);
  g_unix_signal_add (SIGUSR2, (GSourceFunc) quit_main_loop, main_loop);

  g_main_loop_run (main_loop);

  record_logout_for_all_remaining_sessions ();

  g_main_loop_unref (main_loop);
  g_clear_object (&systemd_dbus_proxy);
  g_clear_object (&login_dbus_proxy);
  g_clear_object (&location_file_monitor);

  return EXIT_SUCCESS;
}
