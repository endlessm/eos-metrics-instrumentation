/* Copyright 2020 Endless Mobile, Inc. */

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

#include "eins-location-label.h"

static void
test_empty_keyfile (void)
{
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  g_autoptr(GVariant) payload = build_location_label_event (kf);

  g_assert_null (payload);
}

static void
test_empty_group (void)
{
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  gboolean ret = g_key_file_load_from_data (kf, "[Label]\n", -1, G_KEY_FILE_NONE, NULL);
  g_assert_true (ret);

  g_autoptr(GVariant) payload = build_location_label_event (kf);
  g_assert_null (payload);
}

static void
test_only_populated_keys (void)
{
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  g_key_file_set_string (kf, "Label", "facility", "Aperture Science");
  g_key_file_set_string (kf, "Label", "city", "Unknown");

  g_autoptr(GVariant) payload = build_location_label_event (kf);
  g_assert_nonnull (payload);

  g_assert_cmpuint (g_variant_n_children (payload), ==, 2);

  const gchar *value;
  gboolean ret;

  ret = g_variant_lookup (payload, "facility", "&s", &value);
  g_assert_true (ret);
  g_assert_cmpstr (value, ==, "Aperture Science");

  ret = g_variant_lookup (payload, "city", "&s", &value);
  g_assert_true (ret);
  g_assert_cmpstr (value, ==, "Unknown");
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/location-label/empty", test_empty_keyfile);
  g_test_add_func ("/location-label/empty-group", test_empty_group);
  g_test_add_func ("/location-label/only-populated-keys", test_only_populated_keys);

  return g_test_run ();
}
