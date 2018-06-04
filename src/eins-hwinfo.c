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
#include <json-glib/json-glib.h>

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
 * CPUs in the system. Reported once at system startup. The payload is an array
 * of triples -- a(sqd) -- containing the following information for each group
 * of similar cores/threads:
 *
 * Field | Type   | Description              | Default if unknown
 * ------+--------+--------------------------+-------------------
 *     0 | string | Human-readable CPU model | ''
 *     1 | uint16 | Number of cores/threads  | 0
 *     2 | double | Maximum speed in MHz     | 0.
 *
 * For example, a laptop fitted with an i7-5500U (which has 2 physical cores,
 * each with 2 threads) will be reported as:
 *
 *  [
 *    ('Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz', 4, 3000.)
 *  ]
 *
 * In principle, an ARM big.LITTLE system would have two elements in this
 * array, containing details of the big and LITTLE cores. In practice, the
 * current implementation only reports the currently-active cores.
 */
#define CPU_MODELS_EVENT "4a75488a-0d9a-4c38-8556-148f500edaf0"

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

typedef struct _LscpuFieldType {
  /* NULL-terminated list of names of fields to use from `lscpu --json` output,
   * in order of preference.  Note that this output includes trailing colons in
   * field names, which is presumably a bug caused by re-using the field names
   * from the colon-separated default output.
   */
  const gchar *names[3];

  /* If NULL, the value should be treated as a string. */
  const GVariantType *type;

  /* Default value to use if the field can't be found in `lscpu --json` output,
   * as a serialized GVariant if 'type' is non-NULL or a verbatim string
   * otherwise.
   */
  const gchar *default_value;
} LscpuFieldType;

static const LscpuFieldType LSCPU_FIELDS[] = {
  { { "Model name:", NULL }, NULL, "" },
  { { "CPU(s):", NULL }, G_VARIANT_TYPE_UINT16, "0" },
  /* From manual testing, CPU max MHz is not know within a VirtualBox VM. */
  { { "CPU max MHz:", "CPU MHz:", NULL }, G_VARIANT_TYPE_DOUBLE, "0" },
};
static const gsize NUM_LSCPU_FIELDS = G_N_ELEMENTS (LSCPU_FIELDS);

static GVariant *
parse_field (const LscpuFieldType *ft,
             const gchar          *data)
{
  g_autoptr(GError) error = NULL;
  GVariant *value = NULL;

  g_return_val_if_fail (ft != NULL, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  if (ft->type == NULL)
    {
      value = g_variant_new_string (data);
    }
  else if (!(value = g_variant_parse (ft->type, data, NULL, NULL,
                                      &error)))
    {
      g_debug ("failed to parse %s '%s': %s", ft->names[0], data,
               error->message);
      g_clear_error (&error);
    }

  return value;
}

static const gchar *
object_get_string_member (JsonObject  *object,
                          const gchar *key)
{
  /* You'd hope that json_object_get_string_member() would gracefully return
   * NULL in the case where the value is not a string, but it calls
   * g_return_val_if_fail().  json-glib 1.6 has
   * json_object_get_string_member_with_default() which looks more promising
   * but it still criticals in the case where the value is non-scalar.
   *
   * https://gitlab.gnome.org/GNOME/json-glib/merge_requests/11
   */
  JsonNode *value = json_object_get_member (object, key);

  if (value == NULL)
    return NULL;

  return json_node_get_string (value);
}

GVariant *
eins_hwinfo_parse_lscpu_json (const gchar *json_data,
                              gssize       json_size)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root, *lscpu_node;
  JsonObject *root_object;
  JsonArray *lscpu_array;
  int i, n;
  /* (const gchar *) => (const gchar *), both borrowed from JSON-land */
  g_autoptr(GHashTable) fields = g_hash_table_new (g_str_hash, g_str_equal);
  GVariant *elements[NUM_LSCPU_FIELDS];
  GVariant *payload;
  g_autoptr(GError) error = NULL;

  memset (elements, 0, sizeof elements);

  if (!json_parser_load_from_data (parser,
                                   json_data,
                                   json_size,
                                   &error))
    {
      g_debug ("failed to parse lscpu --json output: %s", error->message);
    }
  else if (!(root = json_parser_get_root (parser))
           || !JSON_NODE_HOLDS_OBJECT (root)
           || !(root_object = json_node_get_object (root))
           || !(lscpu_node = json_object_get_member (root_object, "lscpu"))
           || !JSON_NODE_HOLDS_ARRAY (lscpu_node)
           || !(lscpu_array = json_node_get_array (lscpu_node)))
    {
      g_debug ("lscpu --json didn't have expected structure");
    }
  else
    {
      for (i = 0, n = json_array_get_length (lscpu_array); i < n; i++)
        {
          JsonNode *element_node = json_array_get_element (lscpu_array, i);
          JsonObject *element;
          const gchar *field, *data;

          if (!JSON_NODE_HOLDS_OBJECT (element_node))
            {
              g_debug ("array contained non-object element");
              continue;
            }

          element = json_array_get_object_element (lscpu_array, i);

          if (!(field = object_get_string_member (element, "field"))
              || !(data = object_get_string_member (element, "data")))
            {
              g_debug ("element had no string at key %s",
                       field == NULL ? "field" : "data");
              continue;
            }

          if (!g_hash_table_replace (fields, (gpointer) field, (gpointer) data))
            g_debug ("Already seen %s", field);
        }
    }

  for (i = 0; i < NUM_LSCPU_FIELDS; i++)
    {
      const LscpuFieldType *ft = &LSCPU_FIELDS[i];
      const gchar * const *name = ft->names;

      for (; elements[i] == NULL && *name != NULL; name++)
        {
          const gchar *data = g_hash_table_lookup (fields, *name);

          if (data != NULL)
            elements[i] = parse_field (ft, data);
        }

      if (elements[i] == NULL)
        {
          elements[i] = parse_field (ft, ft->default_value);
          g_assert (elements[i] != NULL);
        }
    }

  /* Sinks floating refs in 'elements' */
  payload = g_variant_new_tuple (elements, NUM_LSCPU_FIELDS);

  /* Right now, this output format from lscpu can only report one collection of
   * CPUs. In principle we'd want to report both sets of cores of an ARM
   * big.LITTLE device separately, so wrap this one element in an array to
   * allow the same event ID to be used in future.
   */
  return g_variant_new_array (NULL, &payload, 1);
}

GVariant *
eins_hwinfo_get_cpu_info (void)
{
  g_autoptr(GSubprocess) lscpu = NULL;
  g_autoptr(GBytes) lsusb_stdout = NULL;
  g_autoptr(JsonParser) parser = json_parser_new ();
  const gchar *json_data = NULL;
  gsize json_size;
  g_autoptr(GError) error = NULL;
  size_t i;

  lscpu = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error,
                            "lscpu", "--json", NULL);
  if (lscpu == NULL
      || !g_subprocess_communicate (lscpu,
                                    NULL /* stdin */,
                                    NULL /* cancellable */,
                                    &lsusb_stdout,
                                    NULL /* stderr */,
                                    &error)
      || !g_subprocess_wait_check (lscpu, NULL /* cancellable */, &error))
    {
      g_warning ("error running lscpu: %s", error->message);
      return NULL;
    }

  json_data = g_bytes_get_data (lsusb_stdout, &json_size);
  g_return_val_if_fail (json_size <= G_MAXSSIZE, NULL);
  return eins_hwinfo_parse_lscpu_json (json_data, (gssize) json_size);
}

static gboolean
record_cpu_models (gpointer unused)
{
  GVariant *payload = eins_hwinfo_get_cpu_info ();

  if (payload != NULL)
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
