/* Copyright © 2017, 2020 Endless Mobile, Inc.
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

#include <flatpak.h>
#include <ostree.h>

#include <eosmetrics/eosmetrics.h>

#define PROGRAM_DUMPED_CORE_EVENT "ed57b607-4a56-47f1-b1e4-5dc3e74335ec"
#define EXPECTED_NUMBER_ARGS 3

typedef struct
{
  FlatpakInstalledRef *app;
  FlatpakInstalledRef *runtime;
} FlatpakInfo;

static FlatpakInfo *
flatpak_info_new (FlatpakInstalledRef *app, FlatpakInstalledRef *runtime)
{
  FlatpakInfo *info = g_slice_new0 (FlatpakInfo);
  info->app = g_object_ref (app);
  info->runtime = g_object_ref (runtime);
  return info;
}

static void
flatpak_info_free (FlatpakInfo *info)
{
  g_clear_object (&info->app);
  g_clear_object (&info->runtime);
  g_slice_free (FlatpakInfo, info);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakInfo, flatpak_info_free)

static void
report_crash (const char *binary,
              gint16 signal,
              gint64 timestamp,
              const char *ostree_commit,
              const char *ostree_url,
              const char *ostree_version,
              const FlatpakInfo *info,
              const char *app_url,
              const char *runtime_url)
{
  GVariantDict dict;
  g_variant_dict_init (&dict, NULL);

  g_variant_dict_insert_value (&dict, "binary", g_variant_new_string (binary));
  g_variant_dict_insert_value (&dict, "signal", g_variant_new_int16 (signal));
  g_variant_dict_insert_value (&dict, "timestamp", g_variant_new_int16 (timestamp));
  g_variant_dict_insert_value (&dict, "ostree_commit", g_variant_new_string (ostree_commit));
  g_variant_dict_insert_value (&dict, "ostree_url", g_variant_new_string (ostree_url));
  if (ostree_version != NULL)
    g_variant_dict_insert_value (&dict, "ostree_version", g_variant_new_string (ostree_version));

  if (info != NULL)
    {
      g_variant_dict_insert_value (&dict, "app_ref",
                                   g_variant_new_take_string (flatpak_ref_format_ref (FLATPAK_REF (info->app))));
      g_variant_dict_insert_value (&dict, "app_commit",
                                   g_variant_new_string (flatpak_ref_get_commit (FLATPAK_REF (info->app))));
      g_variant_dict_insert_value (&dict, "app_url", g_variant_new_string (app_url));
      g_variant_dict_insert_value (&dict, "runtime_ref",
                                   g_variant_new_take_string (flatpak_ref_format_ref (FLATPAK_REF (info->runtime))));
      g_variant_dict_insert_value (&dict, "runtime_commit",
                                   g_variant_new_string (flatpak_ref_get_commit (FLATPAK_REF (info->runtime))));
      g_variant_dict_insert_value (&dict, "runtime_url", g_variant_new_string (runtime_url));
    }

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
get_ostree_repo_url (OstreeRepo *repo, const char *origin)
{
  GKeyFile *config = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *group = g_strdup_printf ("remote \"%s\"", origin);
  char *url = NULL;

  config = ostree_repo_get_config (repo);

  if (!(url = g_key_file_get_string (config, group, "url", &error)))
    {
      g_warning ("Unable to read OSTree config for eos remote URL: %s", error->message);
      return NULL;
    }

  return url;
}

static gboolean
get_eos_ostree_deployment_commit (OstreeSysroot *sysroot,
                                  OstreeRepo    *repo,
                                  char         **commit_out,
                                  char         **version_out)
{
  OstreeDeployment *deployment = ostree_sysroot_get_booted_deployment (sysroot);
  const char *csum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GVariant) commit_metadata = NULL;
  const char *version = NULL;

  g_return_val_if_fail (commit_out == NULL || *commit_out == NULL, FALSE);
  g_return_val_if_fail (version_out == NULL || *version_out == NULL, FALSE);

  if (!deployment)
    {
      g_warning ("OSTree deployment is not currently booted, cannot read state");
      return FALSE;
    }

  csum = ostree_deployment_get_csum (deployment);

  /* Load the backing commit; shouldn't normally fail, but if it does,
   * we stumble on.
   */
  if (ostree_repo_load_variant (repo, OSTREE_OBJECT_TYPE_COMMIT, csum,
                                &commit, NULL))
    commit_metadata = g_variant_get_child_value (commit, 0);

  if (commit_metadata)
    (void) g_variant_lookup (commit_metadata, OSTREE_COMMIT_META_KEY_VERSION, "&s", &version);

  *commit_out = g_strdup (csum);
  *version_out = g_strdup (version);
  return TRUE;
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

static char *
get_associated_runtime (FlatpakInstalledRef *ref, GError **error)
{
  g_autoptr(GBytes) metadata = NULL;
  gchar *runtime = NULL;
  g_autoptr(GKeyFile) key_file = NULL;

  metadata = flatpak_installed_ref_load_metadata (ref, NULL, error);
  if (metadata == NULL)
    return NULL;

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_bytes (key_file, metadata, G_KEY_FILE_NONE, error))
    return NULL;

  if (!(runtime = g_key_file_get_string (key_file, "Application", "runtime", error)))
    return NULL;

  return runtime;
}

static FlatpakInfo *
get_flatpak_info (const char *path,
                  GError **error)
{
  FlatpakInstalledRef *app = NULL;
  g_autoptr(FlatpakInstalledRef) runtime = NULL;
  g_autoptr(GPtrArray) xrefs = NULL;
  guint i;
  g_autofree char *executable_name = NULL;
  g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_system (NULL, error);

  if (installation == NULL)
    return NULL;

  executable_name = g_path_get_basename (path);

  /* we are only interested in apps */
  xrefs = flatpak_installation_list_installed_refs_by_kind (installation,
                                                            FLATPAK_REF_KIND_APP,
                                                            NULL, error);
  if (xrefs == NULL)
    return NULL;

  for (i = 0; i < xrefs->len; i++)
    {
      g_autofree gchar *executable_path = NULL;
      FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);

      executable_path = g_build_filename (flatpak_installed_ref_get_deploy_dir (xref),
                                          "files",
                                          "bin",
                                          executable_name,
                                          NULL);
      /* found a Flatpak with the same application name */
      if (g_file_test (executable_path, G_FILE_TEST_EXISTS))
        {
          app = xref;
          break;
        }
    }

  if (app == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No application with the executable \"%s\" found", executable_name);
      return NULL;
    }

  g_autofree char *runtime_name = get_associated_runtime (app, error);
  if (runtime_name == NULL)
    return NULL;

  g_auto(GStrv) parts = g_strsplit (runtime_name, "/", 3);
  if (g_strv_length (parts) != 3)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can not parse runtime name \"%s\"", runtime_name);
      return NULL;
    }
  runtime = flatpak_installation_get_installed_ref (installation,
                                                    FLATPAK_REF_KIND_RUNTIME,
                                                    parts[0],
                                                    parts[1],
                                                    parts[2],
                                                    NULL,
                                                    error);
  if (runtime == NULL)
    return NULL;

  return flatpak_info_new (app, runtime);
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
  g_autofree char *ostree_url = NULL;
  g_autofree char *ostree_commit = NULL;
  g_autofree char *ostree_version = NULL;
  g_autoptr(FlatpakInfo) flatpak_info = NULL;
  g_autofree char *app_url = NULL;
  g_autofree char *runtime_url = NULL;

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

  if (g_str_has_prefix (path, "/app/bin"))
    {
      g_message ("%s is likely a Flatpak, get information", path);
      flatpak_info = get_flatpak_info (path, &error);
      if (!flatpak_info)
        {
          g_warning ("Unable to get flatpak information: %s", error->message);
          return EXIT_FAILURE;
        }
      app_url = get_ostree_repo_url (repo, flatpak_installed_ref_get_origin (flatpak_info->app));
      runtime_url = get_ostree_repo_url (repo, flatpak_installed_ref_get_origin (flatpak_info->runtime));
      if (!app_url || !runtime_url)
        {
          g_warning ("Unable to get app url or runtime url.");
          return EXIT_FAILURE;
        }
    }

  ostree_url = get_ostree_repo_url (repo, "eos");

  if (!ostree_url || !get_eos_ostree_deployment_commit (sysroot, repo, &ostree_commit, &ostree_version))
    {
      g_warning ("Unable to get OSTree url or commit, perhaps the system has been tampered with?");
      return EXIT_FAILURE;
    }

  report_crash (path, signal, timestamp, ostree_commit, ostree_url, ostree_version, flatpak_info, app_url, runtime_url);

  return EXIT_SUCCESS;
}
