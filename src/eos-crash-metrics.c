/* Copyright 2017 Endless Mobile, Inc. */

#include <stdlib.h>
#include <eosmetrics/eosmetrics.h>
#include <ostree.h>

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

static GHashTable *
map_string_ht_to_variant_ht (GHashTable *input)
{
    GHashTable *output = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                (GDestroyNotify) g_variant_unref);
    g_autoptr(GList) keys = g_hash_table_get_keys (input);
    GList *keys_iter = keys;

    for (; keys_iter; keys_iter = keys_iter->next) {
        g_hash_table_insert (output,
                             g_strdup (keys_iter->data),
                             g_variant_new_string (g_hash_table_lookup (input, keys_iter->data)));
    }

    return output;
}

static void
report_crash (const char *binary,
              gint16 signal,
              gint64 timestamp,
              GHashTable *commits)
{
    g_autoptr(GHashTable) ht = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_variant_unref);
    GVariant *commits_variant = hashtable_to_variant (map_string_ht_to_variant_ht (commits));

    g_hash_table_insert (ht, g_strdup ("binary"), g_variant_new_string (binary));
    g_hash_table_insert (ht, g_strdup ("signal"), g_variant_new_int16 (signal));
    g_hash_table_insert (ht, g_strdup ("timestamp"), g_variant_new_int16 (timestamp));
    g_hash_table_insert (ht, g_strdup ("commits"), commits_variant);

    emtr_event_recorder_record_event_sync (emtr_event_recorder_get_default (),
                                           PROGRAM_DUMPED_CORE_EVENT,
                                           hashtable_to_variant (ht));
}

static GHashTable *
get_ostree_commits (void)
{
    g_autoptr(OstreeSysroot) sysroot = ostree_sysroot_new_default ();
    g_autoptr(OstreeRepo) repo = NULL;
    g_autoptr(GError) error = NULL;
    GHashTable *table = NULL;

    if (!ostree_sysroot_load (sysroot, NULL, &error)) {
        g_warning ("Unable to get current OSTree sysroot: %s", error->message);
        return NULL;
    }

    if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, &error)) {
        g_warning ("Unable to get current OSTree repo: %s", error->message);
        return NULL;
    }

    if (!ostree_repo_list_refs (repo, NULL, &table, NULL, &error)) {
        g_warning ("Unable to list all OSTree refs: %s", error->message);
        return NULL;
    }

    return table;
}

int
main (int argc, char **argv)
{
    const gchar *binary = argv[1];
    const gint16 signal = atoi (argv[2]);
    const gint64 timestamp = atoll (argv[3]);
    g_autoptr(GHashTable) ostree_commits = get_ostree_commits();

    report_crash (binary, signal, timestamp, ostree_commits);

    return 0;
}
