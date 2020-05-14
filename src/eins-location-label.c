/* Copyright 2017–2020 Endless Mobile, Inc. */

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

#include <eosmetrics/eosmetrics.h>

#include "eins-location-label.h"

/*
 * Recorded at startup and whenever location.conf is modified. The auxiliary
 * payload is a dictionary of string keys (such as facility, city and state)
 * to the values provided in the location.conf file. The intention is to allow
 * an operator to provide an optional human-readable label for the location of
 * the system, which can be used when preparing reports or visualisations of the
 * metrics data.
 */
#define LOCATION_LABEL_EVENT "eb0302d8-62e7-274b-365f-cd4e59103983"

#define LOCATION_CONF_FILE SYSCONFDIR "/metrics/location.conf"
#define LOCATION_LABEL_GROUP "Label"

gboolean
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

GFileMonitor *
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

