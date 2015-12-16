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

#include "eins-persistent-tally.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#define FILE_PATH "test_persistent_tally_XXXXXX"
#define GROUP "tallies"
#define KEY "test"
#define KEY_2 "test_two"
#define STARTING_TALLY 18
#define DELTA -3
#define DELTA_2 8
#define STARTING_KEY_FILE \
  "[" GROUP "]\n" \
  KEY "=18\n"

#define OTHER_KEY_FILE \
  "[" GROUP "]\n" \
  KEY "=999\n"

#define EMPTY_KEY_FILE ""

#define CORRUPTED_KEY_FILE \
  "[" GROUP "]\n" \
  KEY "=bananas\n"

/* Helper Functions */

typedef struct
{
  EinsPersistentTally *persistent_tally;
  GFile *tmp_file;
  gchar *tmp_path;
  GKeyFile *key_file;
} Fixture;

static void
write_key_file (Fixture     *fixture,
                const gchar *key_file_data)
{
  GError *error = NULL;
  g_key_file_load_from_data (fixture->key_file, key_file_data, -1,
                             G_KEY_FILE_NONE, &error);
  g_assert_no_error (error);

  g_key_file_save_to_file (fixture->key_file, fixture->tmp_path, &error);
  g_assert_no_error (error);
}

static void
setup (Fixture      *fixture,
       gconstpointer unused)
{
  GError *error = NULL;
  GFileIOStream *stream;
  fixture->tmp_file = g_file_new_tmp (FILE_PATH, &stream, &error);
  g_assert_no_error (error);
  g_object_unref (stream);

  fixture->tmp_path = g_file_get_path (fixture->tmp_file);
  g_assert_nonnull (fixture->tmp_path);

  fixture->key_file = g_key_file_new ();
  write_key_file (fixture, STARTING_KEY_FILE);

  fixture->persistent_tally =
    eins_persistent_tally_new_full (fixture->tmp_path, &error);
  g_assert_no_error (error);
}

static void
teardown (Fixture      *fixture,
          gconstpointer unused)
{
  g_object_unref (fixture->persistent_tally);
  g_key_file_unref (fixture->key_file);
  g_object_unref (fixture->tmp_file);
  g_unlink (fixture->tmp_path);
  g_free (fixture->tmp_path);
}

static void
test_persistent_tally_new_succeeds (Fixture      *fixture,
                                    gconstpointer unused)
{
  g_assert_nonnull (fixture->persistent_tally);
}

static void
test_persistent_tally_can_get_tally (Fixture      *fixture,
                                     gconstpointer unused)
{
  gint64 tally;
  gboolean read_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY, &tally);
  g_assert_true (read_succeeded);
  g_assert_cmpint (tally, ==, STARTING_TALLY);
}

static void
test_persistent_tally_caches_tally (Fixture      *fixture,
                                    gconstpointer unused)
{
  /* This key_file should be ignored by the provider. */
  write_key_file (fixture, OTHER_KEY_FILE);

  gint64 tally;
  gboolean read_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY, &tally);
  g_assert_true (read_succeeded);

  /* The tally should not have changed. */
  g_assert_cmpint (tally, ==, STARTING_TALLY);
}

static void
test_persistent_tally_can_add_to_tally (Fixture      *fixture,
                                        gconstpointer unused)
{
  gboolean write_succeeded =
    eins_persistent_tally_add_to_tally (fixture->persistent_tally, KEY,
                                        DELTA);
  g_assert_true (write_succeeded);

  gint64 tally;
  gboolean read_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY, &tally);
  g_assert_true (read_succeeded);
  g_assert_cmpint (tally, ==, (STARTING_TALLY + DELTA));
}

static void
test_persistent_tally_resets_when_no_file (Fixture      *fixture,
                                           gconstpointer unused)
{
  g_object_unref (fixture->persistent_tally);
  g_assert_cmpint (g_unlink (fixture->tmp_path), ==, 0);

  GError *error = NULL;
  fixture->persistent_tally =
    eins_persistent_tally_new_full (fixture->tmp_path, &error);
  g_assert_no_error (error);

  gint64 tally = -1;
  gboolean read_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY, &tally);

  g_assert_true (read_succeeded);

  g_assert_cmpint (tally, ==, 0);
}

static void
test_persistent_tally_aborts_when_corrupted (Fixture      *fixture,
                                             gconstpointer unused)
{
  g_object_unref (fixture->persistent_tally);
  write_key_file (fixture, CORRUPTED_KEY_FILE);

  GError *error = NULL;
  fixture->persistent_tally =
    eins_persistent_tally_new_full (fixture->tmp_path, &error);
  g_assert_no_error (error);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Could not get tally for key " KEY ". Error: *");
  gint64 tally;
  gboolean read_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY, &tally);
  g_test_assert_expected_messages ();
  g_assert_false (read_succeeded);
  g_assert_cmpint (tally, ==, 0);
}

static void
test_persistent_tally_handles_multiple_keys (Fixture      *fixture,
                                             gconstpointer unused)
{
  gboolean write_succeeded =
    eins_persistent_tally_add_to_tally (fixture->persistent_tally, KEY_2, DELTA_2);
  g_assert_true (write_succeeded);

  gint64 tally_one;
  gboolean read_one_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY,
                                     &tally_one);
  g_assert_true (read_one_succeeded);
  g_assert_cmpint (tally_one, ==, STARTING_TALLY);

  gint64 tally_two;
  gboolean read_two_succeeded =
    eins_persistent_tally_get_tally (fixture->persistent_tally, KEY_2,
                                     &tally_two);
  g_assert_true (read_two_succeeded);
  g_assert_cmpint (tally_two, ==, DELTA_2);
}

gint
main (gint                argc,
      const gchar * const argv[])
{
  g_test_init (&argc, (gchar ***) &argv, NULL);
#define ADD_PERSISTENT_TALLY_TEST_FUNC(path, func) \
  g_test_add ((path), Fixture, NULL, setup, (func), teardown)

  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/new-succeeds",
                                  test_persistent_tally_new_succeeds);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/can-get-tally",
                                  test_persistent_tally_can_get_tally);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/caches-tally",
                                  test_persistent_tally_caches_tally);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/can-add-to-tally",
                                  test_persistent_tally_can_add_to_tally);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/resets-when-no-file",
                                  test_persistent_tally_resets_when_no_file);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/aborts-when-corrupted",
                                  test_persistent_tally_aborts_when_corrupted);
  ADD_PERSISTENT_TALLY_TEST_FUNC ("/persistent-tally/handles-multiple-keys",
                                  test_persistent_tally_handles_multiple_keys);

#undef ADD_PERSISTENT_TALLY_TEST_FUNC

  return g_test_run ();
}
