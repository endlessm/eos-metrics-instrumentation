/* Copyright 2017 Endless Mobile, Inc.
 *
 * This file is part of eos-metrics-instrumentation.
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

#include <string.h>
#include <stdlib.h>
#include <eosmetrics/eosmetrics.h>
#include <ostree.h>

#define PROGRAM_DUMPED_CORE_EVENT "ed57b607-4a56-47f1-b1e4-5dc3e74335ec"
#define EXPECTED_NUMBER_ARGS 3

static void
report_crash (const char *binary,
              gint16 signal,
              gint64 timestamp,
              const char *ostree_commit,
              const char *ostree_url)
{
  GVariantDict dict;
  g_variant_dict_init (&dict, NULL);

  g_variant_dict_insert_value (&dict, "binary", g_variant_new_string (binary));
  g_variant_dict_insert_value (&dict, "signal", g_variant_new_int16 (signal));
  g_variant_dict_insert_value (&dict, "timestamp", g_variant_new_int16 (timestamp));
  g_variant_dict_insert_value (&dict, "ostree_commit", g_variant_new_string (ostree_commit));
  g_variant_dict_insert_value (&dict, "ostree_url", g_variant_new_string (ostree_url));

  emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                         PROGRAM_DUMPED_CORE_EVENT,
                                         g_variant_dict_end (&dict));
}

static OstreeSysroot *
load_ostree_sysroot (GError **error)
{
  OstreeSysroot *sysroot = ostree_sysroot_new_default ();
  if (!ostree_sysroot_load (sysroot, NULL, error))
    return NULL;

  return sysroot;
}

static char *
get_eos_ostree_repo_url (OstreeRepo *repo)
{
  GKeyFile *config = NULL;
  g_autoptr(GError) error = NULL;
  char *url = NULL;

  config = ostree_repo_get_config (repo);

  if (!(url = g_key_file_get_string (config, "remote \"eos\"", "url", &error)))
    {
      g_warning ("Unable to read OSTree config for eos remote URL: %s", error->message);
      return NULL;
    }

  return url;
}

static char *
get_eos_ostree_deployment_commit (OstreeSysroot *sysroot, OstreeRepo *repo)
{
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment (sysroot);
  GKeyFile *config = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *refspec = NULL;
  char *commit = NULL;

  if (!deployment)
    {
      g_warning ("OSTree deployment is not currently booted, cannot read state");
      return NULL;
    }

  config = ostree_deployment_get_origin (deployment);

  if (!(refspec = g_key_file_get_string (config, "origin", "refspec", &error)))
    {
      g_warning ("Unable to read OSTree refspec for booted deployment: %s", error->message);
      return NULL;
    }

  if (!ostree_repo_resolve_rev (repo, refspec, FALSE, &commit, &error))
    {
      g_warning ("Unable to resolve revision for refpsec %s: %s", refspec, error->message);
      return NULL;
    }

  return commit;
}

static const char *blacklisted_prefixes[] =
{
  "/home",
  "/sysroot/home"
};

static gboolean
is_blacklisted_path (const char *path)
{
  const size_t n_blacklisted_prefixes = G_N_ELEMENTS (blacklisted_prefixes);
  size_t i = 0;

  for (; i < n_blacklisted_prefixes; ++i)
    {
      if (g_str_has_prefix (path, blacklisted_prefixes[i]))
        return TRUE;
    }

  return FALSE;
}

/* The kernel gives us paths in the format !usr!bin!myprogram */
static void
normalize_path (char *path)
{
  size_t i = 0;

  for (; i < strlen (path); ++i)
    path[i] = path[i] == '!' ? '/' : path[i];
}


int
main (int argc, char **argv)
{
  gchar *path = argv[1];
  gint16 signal = 0;
  gint64 timestamp = 0;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *url = NULL;
  g_autofree char *commit = NULL;

  if (argc != EXPECTED_NUMBER_ARGS + 1)
    {
      g_warning ("You need to pass three arguments: [binary path] [signal] [timestamp]");
      return EXIT_FAILURE;
    }

  normalize_path (path);
  signal = atoi (argv[2]);
  timestamp = atoll (argv[3]);

  if (is_blacklisted_path (path))
    {
      g_message ("%s is blacklisted, not reporting crash", path);
      return EXIT_SUCCESS;
    }

  sysroot = load_ostree_sysroot (&error);
  if (!sysroot)
    {
      g_warning ("Unable to get current OSTree sysroot: %s", error->message);
      return EXIT_FAILURE;
    }

  if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, &error))
    {
      g_warning ("Unable to read ostree repo from sysroot: %s", error->message);
      return EXIT_FAILURE;
    }

  url = get_eos_ostree_repo_url (repo);
  commit = get_eos_ostree_deployment_commit (sysroot, repo);

  if (!url || !commit)
    {
      g_warning ("Unable to get OSTree url or commit, perhaps the system has been tampered with?");
      return EXIT_FAILURE;
    }

  report_crash (path, signal, timestamp, commit, url);

  return EXIT_SUCCESS;
}
