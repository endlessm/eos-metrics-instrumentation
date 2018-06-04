/* Copyright 2018 Endless Mobile, Inc. */

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
#include "eins-hwinfo.h"

#include <eosmetrics/eosmetrics.h>
#include <gio/gio.h>
#include <glibtop/mem.h>
#include <glibtop/sysinfo.h>

/*
 * Reported at system startup, and every RECORD_DISK_SPACE_INTERVAL_SECONDS
 * after startup. The payload is two triples of int64, ((ttt)(ttt)).
 *
 * The first triple represents the total size, space used, and space available
 * on the / filesystem, measured in bytes. (On dual-boot installations, this
 * refers to the Endless OS image file, not the Windows partition it is hosted
 * on.)
 *
 * On split-disk systems, the second triple represents the /var/endless-extra
 * partition; on single-disk systems, all three values will be 0.
 *
 * Space on other user-accessible partitions on the disk, including Windows
 * partitions on dual-boot systems, is not reported.
 *
 * You might think that given any two of these values for a filesystem, you
 * could derive the third. That's not the case: typically, 5% of space is
 * reserved (so used + available = 0.95 * total) but this is a tunable
 * parameter of the filesystem.
 */
#define DISK_SPACE_EVENT "b76c645f-d041-41b0-a4d4-c29f86c18638"

/* One day */
#define RECORD_DISK_SPACE_INTERVAL_SECONDS (60u * 60u * 24u)

/*
 * The amount of physical memory accessible to Endless OS, in bytes. Reported
 * once at system startup. The payload is a uint64 (t)
 */
#define RAM_SIZE_EVENT "49719ed8-d753-4ba0-9b0d-0abfc65fb95b"

/*
 * CPU models in the system, with the number of threads. Reported once at
 * system startup. The payload is a map from string to uint16 (a{sq}). For
 * example, a laptop fitted with an i7-5500U (which has 2 physical cores, each
 * with 2 threads) will be reported as:
 *
 * {
 *   "Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz": 4,
 * }
 *
 * In the unlikely event that the system has more than 65535 CPU threads, the
 * value will be capped at 65535.
 */
#define CPU_MODELS_EVENT "1d5386c1-ff0f-43d1-b99b-7b2d3e5e2770"

static GVariant *
get_disk_space_for_partition (GFile *file)
{
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  guint64 total = 0;
  guint64 used = 0;
  guint64 available = 0;

  info = g_file_query_filesystem_info (file,
                                       G_FILE_ATTRIBUTE_FILESYSTEM_SIZE ","
                                       G_FILE_ATTRIBUTE_FILESYSTEM_USED ","
                                       G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
                                       NULL /* cancellable */,
                                       &error);
  if (info == NULL)
    {
      g_warning ("%s: %s", G_STRFUNC, error->message);
    }
  else
    {
      total = g_file_info_get_attribute_uint64 (info,
                                                G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
      used = g_file_info_get_attribute_uint64 (info,
                                               G_FILE_ATTRIBUTE_FILESYSTEM_USED);
      available = g_file_info_get_attribute_uint64 (info,
                                                    G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
    }

  return g_variant_new ("(ttt)", total, used, available);
}

static gboolean
is_mounted (GFile *file)
{
  g_autoptr(GFileInfo) info = g_file_query_info (file,
                                                 G_FILE_ATTRIBUTE_UNIX_IS_MOUNTPOINT,
                                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                 NULL, NULL);

  return info != NULL &&
    g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_UNIX_IS_MOUNTPOINT);
}

/**
 * @data: G_SOURCE_REMOVE or G_SOURCE_CONTINUE
 */
static gboolean
record_disk_space (gpointer data)
{
  g_autoptr(GFile) root = g_file_new_for_path ("/");
  g_autoptr(GFile) extra = g_file_new_for_path ("/var/endless-extra");
  GVariant *root_payload = get_disk_space_for_partition (root);
  GVariant *extra_payload = is_mounted (extra)
    ? get_disk_space_for_partition (extra)
    : g_variant_new ("(ttt)", 0, 0, 0);
  GVariant *payload = g_variant_new ("(@(ttt)@(ttt))", root_payload, extra_payload);

  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    DISK_SPACE_EVENT,
                                    g_steal_pointer (&payload));

  return GPOINTER_TO_INT (data);
}

static gboolean
record_ram_size (gpointer unused)
{
  glibtop_mem mem;
  GVariant *payload;

  glibtop_get_mem (&mem);
  payload = g_variant_new_uint64 (mem.total);
  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    RAM_SIZE_EVENT,
                                    g_steal_pointer (&payload));
  return G_SOURCE_REMOVE;
}

/* Derived from gnome-control-center's get_cpu_info(). */
static GVariant *
get_cpu_info (const glibtop_sysinfo *info)
{
  /* Keys: gchar *, borrowed from info
   * Values: guint32
   */
  g_autoptr(GHashTable) counts = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTableIter iter;
  gpointer key, value;
  int i, j;
  GVariantBuilder builder;

  /* count duplicates */
  for (i = 0; i != info->ncpu; ++i)
    {
      const char * const keys[] = { "model name", "cpu", "Processor" };
      char *model;
      int  *count;

      model = NULL;

      for (j = 0; model == NULL && j != G_N_ELEMENTS (keys); ++j)
        {
          model = g_hash_table_lookup (info->cpuinfo[i].values,
                                       keys[j]);
        }

      if (model == NULL)
        continue;

      count = g_hash_table_lookup (counts, model);
      if (count == NULL)
        g_hash_table_insert (counts, model, GUINT_TO_POINTER (1));
      else
        g_hash_table_replace (counts, model, GUINT_TO_POINTER (GPOINTER_TO_UINT (count) + 1));
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
  g_hash_table_iter_init (&iter, counts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *model = key;
      guint count = GPOINTER_TO_INT (value);

      if (count > G_MAXUINT16)
        {
          g_warning ("%s has %u threads; clamping to %" G_GUINT16_FORMAT,
                     model, count, G_MAXUINT16);
          count = G_MAXUINT16;
        }

      g_variant_builder_add (&builder, "{sq}", model, count);
    }

  return g_variant_builder_end (&builder);
}

static gboolean
record_cpu_models (gpointer unused)
{
  const glibtop_sysinfo *info = glibtop_get_sysinfo ();
  GVariant *payload = get_cpu_info (info);

  emtr_event_recorder_record_event (emtr_event_recorder_get_default (),
                                    CPU_MODELS_EVENT,
                                    g_steal_pointer (&payload));
  return G_SOURCE_REMOVE;
}

void
eins_hwinfo_start (void)
{
  g_idle_add (record_disk_space, GINT_TO_POINTER (G_SOURCE_REMOVE));
  g_timeout_add_seconds (RECORD_DISK_SPACE_INTERVAL_SECONDS, record_disk_space,
                         GINT_TO_POINTER (G_SOURCE_CONTINUE));

  g_idle_add (record_ram_size, NULL);
  g_idle_add (record_cpu_models, NULL);
}
