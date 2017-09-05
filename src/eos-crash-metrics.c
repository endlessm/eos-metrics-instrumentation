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
              const char *ostree_commit,
              const char *ostree_url)
{
    g_autoptr(GHashTable) ht = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_variant_unref);

    g_hash_table_insert (ht, g_strdup ("binary"), g_variant_new_string (binary));
    g_hash_table_insert (ht, g_strdup ("signal"), g_variant_new_int16 (signal));
    g_hash_table_insert (ht, g_strdup ("timestamp"), g_variant_new_int16 (timestamp));
    g_hash_table_insert (ht, g_strdup ("ostree_commit"), g_variant_new_string (ostree_commit != NULL ? ostree_commit : ""));
    g_hash_table_insert (ht, g_strdup ("ostree_commit"), g_variant_new_string (ostree_url != NULL ? ostree_url : ""));

    g_message("%s %s", ostree_commit, ostree_url);

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

static OstreeSysroot *
load_ostree_sysroot (void)
{
    OstreeSysroot *sysroot = ostree_sysroot_new_default ();
    g_autoptr(GError) error = NULL;

    if (!ostree_sysroot_load (sysroot, NULL, &error)) {
        g_warning ("Unable to get current OSTree sysroot: %s", error->message);
        return NULL;
    }

    return sysroot;
}

static char *
get_eos_ostree_repo_url (OstreeSysroot *sysroot, OstreeRepo *repo)
{
    GKeyFile *config = NULL;
    g_autoptr(GError) error = NULL;
    char *url = NULL;

    config = ostree_repo_get_config (repo);

    if (!(url = g_key_file_get_string (config, "remote \"eos\"", "url", &error))) {
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
    g_autofree char *commit = NULL;

    if (!deployment) {
        g_warning ("OSTree deployment is not currently booted, cannot read state");
        return NULL;
    }

    config = ostree_deployment_get_origin (deployment);

    if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, &error)) {
        g_warning ("Unable to read repo in sysroot: %s", error->message);
        return NULL;
    }

    if (!(refspec = g_key_file_get_string (config, "origin", "refspec", &error))) {
        g_warning ("Unable to read OSTree refspec for booted deployment: %s", error->message);
        return NULL;
    }

    if (!ostree_repo_resolve_rev (repo, refspec, FALSE, &commit, &error)) {
        g_warning ("Unable to resolve revision for refpsec %s: %s", refspec, error->message);
        return NULL;
    }

    return commit;
}

/* This is the way that the kernel gives us paths ... */
static const char *blacklisted_prefixes[] =
{
    "!home",
    "!sysroot!home"
};

static gboolean
is_blacklisted_path (const char *path)
{
    /* I can never remember that helper macro that does this ... */
    const size_t n_blacklisted_prefixes = sizeof (blacklisted_prefixes) / sizeof (blacklisted_prefixes[0]);
    size_t i = 0;

    for (; i < n_blacklisted_prefixes; ++i) {
        if (g_str_has_prefix (path, blacklisted_prefixes[i]))
            return TRUE;
    }

    return FALSE;
}

int
main (int argc, char **argv)
{
    const gchar *path = argv[1];
    const gint16 signal = atoi (argv[2]);
    const gint64 timestamp = atoll (argv[3]);
    g_autoptr(OstreeSysroot) sysroot = NULL;
    g_autoptr(OstreeRepo) repo = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *url = NULL;
    g_autofree char *commit = NULL;

    if (is_blacklisted_path (path)) {
        g_message ("%s is blacklisted, not reporting crash", path);
        return 0;
    }

    sysroot = load_ostree_sysroot ();

    if (ostree_sysroot_get_repo (sysroot, &repo, NULL, &error)) {
        url = get_eos_ostree_repo_url (sysroot, repo);
        commit = get_eos_ostree_deployment_commit (sysroot, repo);
    } else {
        g_warning ("Unable to read ostree repo from sysroot: %s", error->message);
    }

    report_crash (path, signal, timestamp, url, commit);

    return 0;
}
