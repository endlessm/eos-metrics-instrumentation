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
#include <sys/xattr.h>

#include <eosmetrics/eosmetrics.h>

#include "eins-location.h"
#include "eins-persistent-tally.h"

/*
 * Recorded when startup has finished as defined by the systemd manager DBus
 * interface. The auxiliary payload contains the parameters sent by the DBus
 * systemd manager interface as described at
 * http://www.freedesktop.org/wiki/Software/systemd/dbus/.
 */
#define STARTUP_FINISHED "bf7e8aed-2932-455c-a28e-d407cfd5aaba"

/*
 * Recorded half an hour after the system starts up and then hourly after that.
 * The auxiliary payload is a 2-tuple of the form (uptime_tally, boot_count).
 * uptime_tally is a running total of the system uptime in nanoseconds as a
 * 64-bit signed integer. This running total accumulates across boots and
 * excludes time the computer spends suspended. boot_count is a 64-bit signed
 * integer indicating the 1-based count of the current boot.
 */
#define UPTIME_EVENT "9af2cc74-d6dd-423f-ac44-600a6eee2d96"

/*
 * Recorded when eos-metrics-instrumentation receives the SIGTERM signal, which
 * should generally correspond to system shutdown. The auxiliary payload is the
 * same as that of the uptime event.
 */
#define SHUTDOWN_EVENT "8f70276e-3f78-45b2-99f8-94db231d42dd"

#define UPTIME_KEY "uptime"
#define BOOT_COUNT_KEY "boot_count"

/* This is the period in seconds with which we record the total system uptime
 * across all boots.
 */
#define RECORD_UPTIME_INTERVAL (60u * 60u)

/*
 * Started when a user logs in and stopped when that user logs out.
 * Payload contains the user ID of the user that logged in.
 * (Thus the payload is a GVariant containing a single unsigned 32-bit integer.)
 */
#define USER_IS_LOGGED_IN "add052be-7b2a-4959-81a5-a7f45062ee98"

#define MIN_HUMAN_USER_ID 1000

/*
 * Recorded when the network changes from one of the states described at
 * https://developer.gnome.org/NetworkManager/unstable/spec.html#type-NM_STATE
 * to another. The auxiliary payload is a 2-tuple of the form
 * (previous_network_state, new_network_state). Since events are delivered on a
 * best-effort basis, there is no guarantee that the new network state of the
 * previous successfully recorded network status change event matches the
 * previous network state of the current network status change event.
 */
#define NETWORK_STATUS_CHANGED_EVENT "5fae6179-e108-4962-83be-c909259c0584"

/* Recorded at every startup to track deployment statistics. The auxiliary
 * payload is a 3-tuple of the form (os_name, os_version, eos_personality).
 */
#define OS_VERSION_EVENT "1fa16a31-9225-467e-8502-e31806e9b4eb"

#define OS_RELEASE_FILE "/etc/os-release"

#define PERSONALITY_FILE_PATH "/etc/EndlessOS/personality.conf"
#define PERSONALITY_CONFIG_GROUP "Personality"
#define PERSONALITY_KEY "PersonalityName"

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

/*
 * Recorded once at startup to report the image ID. This is a string such as
 * "eos-eos3.1-amd64-amd64.170115-071322.base" which is saved in an attribute
 * on the root filesystem by the image builder, and allows us to tell the
 * channel that the OS was installed by (eg download, OEM pre-install, Endless
 * hardware, USB stick, etc) and which version was installed. The payload
 * is a single string containing this image ID, if present.
 */

#define EOS_IMAGE_VERSION_EVENT "6b1c1cfc-bc36-438c-0647-dacd5878f2b3"

#define EOS_IMAGE_VERSION_XATTR "user.eos-image-version"
#define EOS_IMAGE_VERSION_PATH "/sysroot"
#define EOS_IMAGE_VERSION_ALT_PATH "/"

static gboolean prev_time_set = FALSE;
static gint64 prev_time;
static EinsPersistentTally *persistent_tally;

static GData *humanity_by_session_id;

static guint32 previous_network_state = 0; // NM_STATE_UNKNOWM

static gboolean
get_os_version (gchar **name_out,
                gchar **version_out)
{
    GError *error = NULL;
    gboolean succeeded = FALSE;
    gchar *name = NULL;
    gchar *version = NULL;

    GFile *os_release_file = g_file_new_for_path (OS_RELEASE_FILE);
    GFileInputStream *file_stream =
      g_file_read (os_release_file, NULL, &error);
    g_object_unref (os_release_file);

    if (error)
      goto out;

    GDataInputStream *data_stream =
      g_data_input_stream_new (G_INPUT_STREAM (file_stream));
    g_object_unref (file_stream);

    while (!name || !version)
      {
        gchar *line =
          g_data_input_stream_read_line (data_stream, NULL, NULL, &error);
        if (!line)
          break;

        if (g_str_has_prefix (line, "NAME="))
          name = line;
        else if (g_str_has_prefix (line, "VERSION="))
          version = line;
        else
          g_free (line);
      }

    g_object_unref (data_stream);

    if (error)
      goto out;

    if (!name || !version)
      {
        g_warning ("Could not find at least one of NAME or VERSION keys in "
                   OS_RELEASE_FILE ".");
        goto out;
      }

    /* According to os-release(5), these values can be quoted, escaped,
     * etc. For simplicity, instead of doing the parsing on the client
     * side, we do it on the server side.
     */
    *name_out = g_strdup (name + strlen ("NAME="));
    *version_out = g_strdup (version + strlen ("VERSION="));

    succeeded = TRUE;

 out:
    if (error)
      {
        g_warning ("Error reading " OS_RELEASE_FILE ": %s.", error->message);
        g_error_free (error);
      }

    g_free (name);
    g_free (version);

    return succeeded;
}

static gchar *
get_eos_personality (void)
{
    gchar *personality = NULL;
    GKeyFile *key_file = g_key_file_new ();

    /* We ignore errors here since the personality file will be
     * missing from e.g. base images.
     */
    if (!g_key_file_load_from_file (key_file, PERSONALITY_FILE_PATH,
                                    G_KEY_FILE_NONE, NULL))
      goto out;

    GError *error = NULL;
    personality = g_key_file_get_string (key_file, PERSONALITY_CONFIG_GROUP,
                                         PERSONALITY_KEY, &error);

    if (error != NULL)
      {
        g_warning ("Could not read " PERSONALITY_KEY " from "
                   PERSONALITY_FILE_PATH ": %s.", error->message);
        g_error_free (error);
      }

 out:
    g_key_file_unref (key_file);

    return (personality != NULL) ? personality : g_strdup ("");
}

static gboolean
record_os_version (gpointer unused)
{
    gchar *os_name = NULL;
    gchar *os_version = NULL;

    if (!get_os_version (&os_name, &os_version))
      return G_SOURCE_REMOVE;

    gchar *eos_personality = get_eos_personality ();

    GVariant *payload = g_variant_new ("(sss)",
                                       os_name, os_version, eos_personality);
    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      OS_VERSION_EVENT, payload);

    g_free (os_name);
    g_free (os_version);
    g_free (eos_personality);

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
      g_error_free (error);
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

static gchar *
get_image_version_for_path (const gchar *path)
{
    ssize_t xattr_size;
    g_autofree gchar *image_version = NULL;

    xattr_size = getxattr (path, EOS_IMAGE_VERSION_XATTR, NULL, 0);

    if (xattr_size < 0 || xattr_size > SSIZE_MAX - 1)
      return NULL;

    image_version = g_malloc0 (xattr_size + 1);

    xattr_size = getxattr (path, EOS_IMAGE_VERSION_XATTR,
                           image_version, xattr_size);

    /* this check is primarily for ERANGE, in case the attribute size has
     * changed from the first call to this one */
    if (xattr_size < 0)
      {
        g_warning ("Error when getting 'eos-image-version' from %s: %s", path,
                   strerror (errno));
        return NULL;
      }

    /* shouldn't happen, but if the filesystem is modified or corrupted, we
     * don't want to cause assertion errors / D-Bus disconnects with invalid
     * UTF-8 strings */
    if (!g_utf8_validate (image_version, xattr_size, NULL))
      {
        g_warning ("Invalid UTF-8 when getting 'eos-image-version' from %s",
                   path);
        return NULL;
      }

    return g_steal_pointer (&image_version);
}

static gchar *
get_image_version (void)
{
    gchar *image_version = get_image_version_for_path (EOS_IMAGE_VERSION_PATH);

    if (image_version == NULL)
      image_version = get_image_version_for_path (EOS_IMAGE_VERSION_ALT_PATH);

    return image_version;
}

static gboolean
record_image_version (gpointer unused)
{
    g_autofree gchar *image_version = NULL;

    image_version = get_image_version ();

    if (image_version != NULL)
      emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                        EOS_IMAGE_VERSION_EVENT,
                                        g_variant_new_string (image_version));

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

/* Returns an auxiliary payload that is a 2-tuple of the form
 * (uptime_tally, boot_count). uptime_tally is the running total uptime across
 * all boots in nanoseconds as a 64-bit signed integer. boot_count is the
 * 1-based count associated with the current boot as a 64-bit signed integer.
 * Returns NULL on error. Sets the global variable prev_time to the current
 * time. Adds the time elapsed since prev_time to the running uptime tally that
 * spans boots.
 */
static GVariant *
make_uptime_payload (void)
{
    gint64 current_time;
    gboolean got_current_time =
      emtr_util_get_current_time (CLOCK_MONOTONIC, &current_time);

    if (!got_current_time || !prev_time_set || persistent_tally == NULL)
      return NULL;

    gint64 time_elapsed = current_time - prev_time;
    gboolean add_succeeded =
      eins_persistent_tally_add_to_tally (persistent_tally, UPTIME_KEY,
                                          time_elapsed);

    if (!add_succeeded)
      return NULL;

    prev_time = current_time;

    gint64 total_uptime;
    gboolean got_uptime =
      eins_persistent_tally_get_tally (persistent_tally, UPTIME_KEY,
                                       &total_uptime);

    if (!got_uptime)
      return NULL;

    gint64 boot_count;
    gboolean got_boot_count =
      eins_persistent_tally_get_tally (persistent_tally, BOOT_COUNT_KEY,
                                       &boot_count);

    if (!got_boot_count)
      return NULL;

    return g_variant_new ("(xx)", total_uptime, boot_count);
}

/* Intended for use as a GSourceFunc callback. Records an uptime event. Reports
 * the running uptime tally that spans across boots and the boot count as the
 * auxiliary payload of the system shutdown event.
 */
static gboolean
record_uptime (gpointer unused)
{
    GVariant *uptime_payload = make_uptime_payload ();
    emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                      UPTIME_EVENT, uptime_payload);
    g_timeout_add_seconds (RECORD_UPTIME_INTERVAL, (GSourceFunc) record_uptime,
                           NULL);
    return G_SOURCE_REMOVE;
}

/* Records a system shutdown event. Reports the running uptime tally that spans
 * across boots and the boot count as the auxiliary payload of the system
 * shutdown event.
 */
static void
record_shutdown (void)
{
    GVariant *uptime_payload = make_uptime_payload ();
    emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                           SHUTDOWN_EVENT, uptime_payload);

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

        GVariant *status_change = g_variant_new ("(uu)", previous_network_state,
                                                 new_network_state);

        emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                          NETWORK_STATUS_CHANGED_EVENT,
                                          status_change);

        previous_network_state = new_network_state;
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
                                     NULL /* GCancellable */,
                                     &error);
    if (dbus_proxy == NULL)
      {
        g_warning ("Error creating GDBusProxy: %s.", error->message);
        g_error_free (error);
      }
    else
      {
        g_signal_connect (dbus_proxy, "g-signal",
                          G_CALLBACK (record_network_change), NULL /* data */);
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
    prev_time_set = emtr_util_get_current_time (CLOCK_MONOTONIC, &prev_time);
    g_datalist_init (&humanity_by_session_id);

    GDBusProxy *systemd_dbus_proxy = systemd_dbus_proxy_new ();
    GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
    GDBusProxy *network_dbus_proxy = network_dbus_proxy_new ();

    GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);

    g_idle_add ((GSourceFunc) record_location_metric, NULL);
    g_idle_add ((GSourceFunc) record_os_version, NULL);
    g_idle_add ((GSourceFunc) increment_boot_count, NULL);
    g_idle_add ((GSourceFunc) record_live_boot, NULL);
    g_idle_add ((GSourceFunc) record_image_version, NULL);
    g_timeout_add_seconds (RECORD_UPTIME_INTERVAL / 2,
                           (GSourceFunc) record_uptime, NULL);

    g_unix_signal_add (SIGHUP, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGINT, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGTERM, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR1, (GSourceFunc) quit_main_loop, main_loop);
    g_unix_signal_add (SIGUSR2, (GSourceFunc) quit_main_loop, main_loop);

    g_main_loop_run (main_loop);

    record_logout_for_all_remaining_sessions ();
    record_shutdown ();

    g_main_loop_unref (main_loop);
    g_clear_object (&systemd_dbus_proxy);
    g_clear_object (&login_dbus_proxy);
    g_clear_object (&network_dbus_proxy);

    return EXIT_SUCCESS;
}
