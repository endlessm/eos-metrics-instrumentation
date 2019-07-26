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

#include "eins-hwinfo.h"
#include "eins-location.h"
#include "eins-network-id.h"
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

/* This is the period (one hour) with which we record the total system uptime
 * across all boots.
 */
#define RECORD_UPTIME_INTERVAL_SECONDS (60u * 60u)

/*
 * Started when a user logs in and stopped when that user logs out.
 * Payload contains the user ID of the user that logged in.
 * (Thus the payload is a GVariant containing a single unsigned 32-bit integer.)
 */
#define USER_IS_LOGGED_IN "add052be-7b2a-4959-81a5-a7f45062ee98"

#define MIN_HUMAN_USER_ID 1000

/*
 * Recorded at startup and whenever location.conf is modified. The auxiliary
 * payload is a dictionary of string keys (such as facility, city and state)
 * to the values provided in the location.conf file. The intention is to allow
 * an operator to provide an optional human-readable label for the location of
 * the system, which can be used when preparing reports or visualisations of the
 * metrics data.
 */
#define LOCATION_LABEL_EVENT "eb0302d8-62e7-274b-365f-cd4e59103983"

/*
 * Recorded when we detect a change in the default route after the network
 * connectivity has changed. The auxiliary payload is a 32-bit unsigned integer
 * containing a hash of the ethernet MAC address of the gateway, favouring
 * IPv4 if available, or IPv6 if not. The intention is to provide a value
 * which is opaque and stable which is the same for every system located
 * on the same physical network.
 */
#define NETWORK_ID_EVENT "38eb48f8-e131-9b57-77c6-35e0590c82fd"

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

/*
 * Reported once at startup to describe whether certain ACPI tables are present
 * on the system. The payload has type u, formed as a bitmask of which ACPI
 * tables are found. The tables we check for are MSDM and SLIC, which hold
 * OEM Windows license information on newer and older systems respectively.
 * The bits are mapped as:
 *
 *  0: no table found, system shipped without Windows
 *  1: MSDM table found, system shipped with newer Windows
 *  2: SLIC table found, system shipped with Vista-era Windows
 *
 * We have not seen systems which have both tables, but they might exist in the
 * wild and would appear with a value of 3. With this information, assuming
 * LIVE_BOOT_EVENT is not sent, then we can distinguish:
 *
 *  SLIC|MSDM | DUAL_BOOT | Meaning
 * -----------+-----------+----------------------------------------------------
 *    >0      |   false   | Endless OS is the sole OS, PC came with Windows
 *    >0      |   true    | Endless OS installed alongside OEM Windows
 *     0      |   false   | Endless OS is the sole OS, PC came without Windows
 *     0      |   true    | Dual-booting with a retail Windows
 */
#define WINDOWS_LICENSE_TABLES_EVENT "ef74310f-7c7e-ca05-0e56-3e495973070a"
#define ACPI_TABLES_PATH "/sys/firmware/acpi/tables"
static const gchar * const windows_license_tables[] = { "MSDM", "SLIC" };

static gboolean prev_time_set = FALSE;
static gint64 prev_time;
static EinsPersistentTally *persistent_tally;

static GData *humanity_by_session_id;

static guint32 previous_network_id = 0;
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

static gboolean
record_os_version (gpointer unused)
{
  gchar *os_name = NULL;
  gchar *os_version = NULL;

  if (!get_os_version (&os_name, &os_version))
    return G_SOURCE_REMOVE;

  GVariant *payload = g_variant_new ("(sss)",
                                     os_name, os_version, "");
  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    OS_VERSION_EVENT, payload);

  g_free (os_name);
  g_free (os_version);

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
record_image_version (const char *image_version)
{
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
  g_timeout_add_seconds (RECORD_UPTIME_INTERVAL_SECONDS,
                         (GSourceFunc) record_uptime,
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

#define LOCATION_CONF_FILE SYSCONFDIR "/metrics/location.conf"
#define LOCATION_LABEL_GROUP "Label"

static gboolean
record_location_label (gpointer unused)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GKeyFile) kf = g_key_file_new ();

  if (!g_key_file_load_from_file (kf, LOCATION_CONF_FILE, G_KEY_FILE_NONE, &err))
    {
      /* this file’s existence is optional, so not found is not an error */
      if (g_error_matches (err, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND))
        return G_SOURCE_REMOVE;

      g_warning ("Failed to load " LOCATION_CONF_FILE ", unable to record location label: %s",
                 err->message);
      return G_SOURCE_REMOVE;
    }

  g_auto (GStrv) keys = g_key_file_get_keys (kf, LOCATION_LABEL_GROUP, NULL, NULL);
  if (keys == NULL || *keys == NULL)
    return G_SOURCE_REMOVE;

  g_autoptr (GString) label = g_string_new ("");
  g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_ARRAY);
  for (GStrv cur = keys; *cur != NULL; cur++)
    {
      const gchar *key = *cur;
      g_autofree gchar *val = g_key_file_get_string (kf, LOCATION_LABEL_GROUP, key, NULL);

      if (val == NULL)
        continue;

      if (cur != keys)
        g_string_append (label, ", ");

      g_variant_builder_add (&builder, "{ss}", key, val);
      g_string_append_printf (label, "\"%s\" = \"%s\"", key, val);
    }

  g_message ("Recording location label: %s", label->str);

  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    LOCATION_LABEL_EVENT,
                                    g_variant_builder_end (&builder));

  return G_SOURCE_REMOVE;
}

static void
on_location_file_changed (GFileMonitor *monitor,
                          GFile *file,
                          GFile *other_file,
                          GFileMonitorEvent event_type,
                          gpointer user_data)
{
  if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    record_location_label (NULL);
}

static GFileMonitor *
location_file_monitor_new (void)
{
  g_autoptr (GFile) file = g_file_new_for_path (LOCATION_CONF_FILE);
  g_autoptr (GError) err = NULL;
  g_autoptr (GFileMonitor) monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE,
                                                          NULL, &err);

  if (err != NULL)
    {
      g_warning ("Couldn't set up file monitor for " LOCATION_CONF_FILE ": %s",
                 err->message);
      return NULL;
    }

  g_signal_connect (monitor, "changed", G_CALLBACK (on_location_file_changed),
                    NULL);

  return g_steal_pointer (&monitor);
}

static void
record_network_id_impl (const char *image_version,
                        gboolean force)
{
  guint32 network_id;

  /* Network ID is only needed for analysis on Solutions images */
  if (!image_version || !g_str_has_prefix (image_version, "solutions-"))
    {
      g_message ("Not recording network ID as this is not a Solutions system");
      return;
    }

  if (!eins_network_id_get (&network_id))
    return;

  if (network_id != previous_network_id || force)
    {
      g_message ("Recording network ID: %8x", network_id);

      emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                        NETWORK_ID_EVENT,
                                        g_variant_new_uint32 (network_id));

      previous_network_id = network_id;
    }
}

static gboolean
record_network_id_force (const char *image_version)
{
  record_network_id_impl (image_version, TRUE);
  return G_SOURCE_REMOVE;
}

static gboolean
record_network_id (const char *image_version)
{
  record_network_id_impl (image_version, FALSE);
  return G_SOURCE_REMOVE;
}

/* from https://developer.gnome.org/NetworkManager/unstable/nm-dbus-types.html#NMState */
#define NM_STATE_CONNECTED_SITE 60

static void
record_network_change (GDBusProxy *dbus_proxy,
                       gchar      *sender_name,
                       gchar      *signal_name,
                       GVariant   *parameters,
                       const char *image_version)
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

      /* schedule recording the network ID provided we have a default route */
      if (new_network_state >= NM_STATE_CONNECTED_SITE)
        {
          g_idle_add ((GSourceFunc) record_network_id,
                      (gpointer) image_version);
        }

      previous_network_state = new_network_state;
    }
}

static GDBusProxy *
network_dbus_proxy_new (const char *image_version)
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
                        G_CALLBACK (record_network_change),
                        (gpointer) image_version);
    }
  return dbus_proxy;
}

static gboolean
record_windows_licenses (gpointer unused)
{
  g_autoptr(GFile) tables = g_file_new_for_path (ACPI_TABLES_PATH);
  guint32 licenses = 0;
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (windows_license_tables); i++)
    {
      const gchar *table_name = windows_license_tables[i];
      g_autoptr(GFile) table = g_file_get_child (tables, table_name);
      gboolean present = g_file_query_exists (table, NULL);

      g_debug ("ACPI table %s is %s",
               table_name,
               present ? "present" : "absent");

      if (present)
        licenses |= 1 << i;
    }

  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    WINDOWS_LICENSE_TABLES_EVENT,
                                    g_variant_new_uint32 (licenses));

  return G_SOURCE_REMOVE;
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

  g_autofree char *image_version = get_image_version ();

  GDBusProxy *systemd_dbus_proxy = systemd_dbus_proxy_new ();
  GDBusProxy *login_dbus_proxy = login_dbus_proxy_new ();
  GDBusProxy *network_dbus_proxy = network_dbus_proxy_new (image_version);
  GFileMonitor *location_file_monitor = location_file_monitor_new ();

  GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);

  g_idle_add ((GSourceFunc) record_location_metric, NULL);
  g_idle_add ((GSourceFunc) record_os_version, NULL);
  g_idle_add ((GSourceFunc) increment_boot_count, NULL);
  g_idle_add ((GSourceFunc) record_live_boot, NULL);
  g_idle_add ((GSourceFunc) record_image_version, image_version);
  g_idle_add ((GSourceFunc) record_location_label, NULL);
  g_idle_add ((GSourceFunc) record_network_id_force, image_version);
  g_idle_add ((GSourceFunc) record_windows_licenses, NULL);
  g_timeout_add_seconds (RECORD_UPTIME_INTERVAL_SECONDS / 2,
                         (GSourceFunc) record_uptime, NULL);

  eins_hwinfo_start ();

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
  g_clear_object (&location_file_monitor);

  return EXIT_SUCCESS;
}
