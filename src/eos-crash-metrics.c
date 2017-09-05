/* Copyright 2017 Endless Mobile, Inc. */

#include <stdlib.h>
#include <eosmetrics/eosmetrics.h>

#define PROGRAM_DUMPED_CORE_EVENT "ed57b607-4a56-47f1-b1e4-5dc3e74335ec"

static GVariant *
hashtable_to_variant (GHashTable *ht)
{
    g_autoptr(GList) keys = g_hash_table_get_keys (ht);
    g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

    GList *key_iter = keys;
    for (; key_iter; key_iter = key_iter->next) {
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("{sv}"));

        g_variant_builder_add (&builder, "s", g_strdup (key_iter->data));
        g_variant_builder_add (&builder, "v", g_variant_ref (g_hash_table_lookup (ht, key_iter->data)));

        g_variant_builder_close (&builder);
    }

    return g_variant_builder_end (&builder);
}

static void
report_crash (const char *binary, gint16 signal, gint64 timestamp)
{
    g_autoptr(GHashTable) ht = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_variant_unref);

    g_hash_table_insert (ht, g_strdup ("binary"), g_variant_new_string (binary));
    g_hash_table_insert (ht, g_strdup ("signal"), g_variant_new_int16 (signal));
    g_hash_table_insert (ht, g_strdup ("timestamp"), g_variant_new_int16 (timestamp));

    emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                           PROGRAM_DUMPED_CORE_EVENT,
                                           hashtable_to_variant (ht));
}

int
main (int argc, char **argv) {
    const gchar *binary = argv[1];
    const gint16 signal = atoi (argv[2]);
    const gint64 timestamp = atoll (argv[3]);

    report_crash (binary, signal, timestamp);

    return 0;
}
