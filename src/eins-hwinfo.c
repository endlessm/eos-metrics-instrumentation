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

void
eins_hwinfo_start (void)
{
  g_idle_add (record_disk_space, GINT_TO_POINTER (G_SOURCE_REMOVE));
  g_timeout_add_seconds (RECORD_DISK_SPACE_INTERVAL_SECONDS, record_disk_space,
                         GINT_TO_POINTER (G_SOURCE_CONTINUE));
}
