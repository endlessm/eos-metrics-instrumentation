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

#include <glib.h>
#include <eosmetrics/eosmetrics.h>

#include "eins-location.h"
#include "geoclue.h"

#define DESKTOP_ID "eos-metrics-instrumentation"

/* This is from geoclue private header src/public-api/gclue-enums.h */
#define GCLUE_ACCURACY_LEVEL_CITY 4
/* This should be kept in sync with geocode-glib/geocode-glib.h */
#define LOCATION_ACCURACY_CITY 15000 /* m */

/*
 * Recorded once per boot. The payload contains the following information:
 *   latitude (double)
 *   longitude (double)
 *   is altitude known? (boolean)
 *   altitude (double) -- garbage if altitude is not known
 *   accuracy of this location (double)
 */
#define EVENT_USER_LOCATION "abe7af92-6704-4d34-93cf-8f1b46eb09b8"

static void
on_location_proxy_ready (GObject      *obj,
                         GAsyncResult *res)
{
  GError *error = NULL;

  GeoclueLocation *location = geoclue_location_proxy_new_for_bus_finish (res,
                                                                         &error);
  if (error != NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_critical ("Failed to get location from GeoClue: %s.", error->message);
      g_error_free (error);
      return;
    }

  gdouble latitude = geoclue_location_get_latitude (location);
  gdouble longitude = geoclue_location_get_longitude (location);
  gdouble altitude = geoclue_location_get_altitude (location);
  gdouble accuracy = geoclue_location_get_accuracy (location);
  /* altitude is set to -1.7976931348623157e+308 if not determined */
  gboolean altitude_known = (altitude > -1e308);

  GVariant *location_info = g_variant_new ("(ddbdd)", latitude, longitude,
                                           altitude_known, altitude, accuracy);

  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    EVENT_USER_LOCATION, location_info);
  /* Assumes ownership of location_info */

  g_object_unref (location);
}

static void
on_location_updated (GeoclueClient *client,
                     gchar         *location_path_old,
                     gchar         *location_path_new)
{
  geoclue_location_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      "org.freedesktop.GeoClue2",
                                      location_path_new,
                                      NULL, /* cancellable */
                                      (GAsyncReadyCallback) on_location_proxy_ready,
                                      NULL /* data */);
  g_object_unref (client);
}

static void
on_start_ready (GeoclueClient *client,
                GAsyncResult  *res)
{
  GError *error = NULL;

  if (!geoclue_client_call_start_finish (client, res, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_critical ("Failed to start GeoClue2 client: %s.", error->message);
      g_error_free (error);
    }
}

static void
on_client_proxy_ready (GObject      *obj,
                       GAsyncResult *res)
{
  GError *error = NULL;

  GeoclueClient *client = geoclue_client_proxy_new_for_bus_finish (res, &error);
  if (error != NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_critical ("Failed to get GeoClue client: %s.", error->message);
      g_error_free (error);
      return;
    }

  geoclue_client_set_desktop_id (client, DESKTOP_ID);
  geoclue_client_set_distance_threshold (client, LOCATION_ACCURACY_CITY);
  geoclue_client_set_requested_accuracy_level (client,
                                               GCLUE_ACCURACY_LEVEL_CITY);

  g_signal_connect (client, "location-updated",
                    G_CALLBACK (on_location_updated), NULL);

  geoclue_client_call_start (client, NULL, /* cancellable */
                             (GAsyncReadyCallback) on_start_ready, NULL);
}

static void
on_get_client_ready (GeoclueManager *manager,
                     GAsyncResult   *res)
{
  gchar *client_path;
  GError *error = NULL;

  if (!geoclue_manager_call_get_client_finish (manager, &client_path, res,
                                               &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_critical ("Failed to get GeoClue client: %s.", error->message);
      g_error_free (error);
      return;
    }

  geoclue_client_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
                                    "org.freedesktop.GeoClue2", client_path,
                                    NULL, /* cancellable */
                                    (GAsyncReadyCallback) on_client_proxy_ready,
                                    NULL /* data */);

  g_free (client_path);
}

static void
on_manager_proxy_ready (GObject      *obj,
                        GAsyncResult *res)
{
  GError *error = NULL;

  GeoclueManager *manager = geoclue_manager_proxy_new_for_bus_finish (res,
                                                                      &error);
  if (error != NULL)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_critical ("Failed to get GeoClue manager object: %s.",
                    error->message);
      g_error_free (error);
      return;
    }

  geoclue_manager_call_get_client (manager, NULL,  /* cancellable */
                                   (GAsyncReadyCallback) on_get_client_ready,
                                   NULL /* data */);
  g_object_unref (manager);
}

/*
 * record_location_metric:
 *
 * Access GeoClue to record the user's location, on certain partner images only.
 *
 * Returns: %G_SOURCE_REMOVE for use as an idle function.
 */
gboolean
record_location_metric (const char *image_version)
{
  /* Location is only needed for analysis on certain partner images */
  if (!image_version ||
      !(g_str_has_prefix (image_version, "fnde-") ||
        g_str_has_prefix (image_version, "impact-") ||
        g_str_has_prefix (image_version, "solutions-")))
    {
      g_message ("Not recording location as it is not required for this image");
      return G_SOURCE_REMOVE;
    }

  geoclue_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     "org.freedesktop.GeoClue2",
                                     "/org/freedesktop/GeoClue2/Manager",
                                     NULL,  /* cancellable */
                                     (GAsyncReadyCallback) on_manager_proxy_ready,
                                     NULL  /* data */);
  return G_SOURCE_REMOVE;
}
