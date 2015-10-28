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

#include <errno.h>
#include <gio/gio.h>

typedef struct EinsPersistentTallyPrivate
{
  GKeyFile *key_file;
  gchar *file_path;
  gchar *key;
  gint64 tally;
  gboolean tally_cached;
} EinsPersistentTallyPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (EinsPersistentTally, eins_persistent_tally, G_TYPE_OBJECT)

/* The path to the key file containing the running tally. */
#define DEFAULT_FILE_PATH INSTRUMENTATION_CACHE_DIR "persistent-tallies"

#define GROUP "tallies"

#define MODE 02775

enum {
  PROP_0,
  PROP_FILE_PATH,
  PROP_KEY,
  NPROPS
};

static GParamSpec *eins_persistent_tally_props[NPROPS] = { NULL, };

static void
eins_persistent_tally_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  EinsPersistentTally *self = EINS_PERSISTENT_TALLY (object);
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  switch (property_id)
    {
    case PROP_FILE_PATH:
      priv->file_path = g_value_dup_string (value);
      break;

    case PROP_KEY:
      priv->key = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
eins_persistent_tally_finalize (GObject *object)
{
  EinsPersistentTally *self = EINS_PERSISTENT_TALLY (object);
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  g_key_file_unref (priv->key_file);
  g_free (priv->file_path);
  g_free (priv->key);

  G_OBJECT_CLASS (eins_persistent_tally_parent_class)->finalize (object);
}

static void
eins_persistent_tally_class_init (EinsPersistentTallyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  eins_persistent_tally_props[PROP_FILE_PATH] =
    g_param_spec_string ("file-path", "File path",
                         "The path to the file where the tally is stored.",
                         DEFAULT_FILE_PATH,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  eins_persistent_tally_props[PROP_KEY] =
    g_param_spec_string ("key", "Key",
                         "Used to lookup a specific tally in the file.",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  object_class->set_property = eins_persistent_tally_set_property;
  object_class->finalize = eins_persistent_tally_finalize;

  g_object_class_install_properties (object_class, NPROPS,
                                     eins_persistent_tally_props);
}

static void
eins_persistent_tally_init (EinsPersistentTally *self)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);
  priv->key_file = g_key_file_new ();
}

EinsPersistentTally *
eins_persistent_tally_new (const gchar *key)
{
  return g_object_new (EINS_TYPE_PERSISTENT_TALLY,
                       "key", key,
                       NULL);
}

EinsPersistentTally *
eins_persistent_tally_new_full (const gchar *file_path,
                                const gchar *key)
{
  return g_object_new (EINS_TYPE_PERSISTENT_TALLY,
                       "file-path", file_path,
                       "key", key,
                       NULL);
}

static gboolean
write_tally (EinsPersistentTally *self,
             gint64               tally)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  GFile *file = g_file_new_for_path (priv->file_path);
  GFile *parent_file = g_file_get_parent (file);
  g_object_unref (file);

  if (parent_file != NULL)
    {
      gchar *parent_path = g_file_get_path (parent_file);
      g_object_unref (parent_file);

      if (parent_path != NULL)
        {
          gint status_code = g_mkdir_with_parents (parent_path, MODE);
          if (status_code != 0)
            {
              gint error_number = errno;
              g_free (parent_path);
              g_critical ("Failed to create directory. Error: %s.",
                          g_strerror (error_number));
              return FALSE;
            }
          g_free (parent_path);
        }
    }

  g_key_file_set_int64 (priv->key_file, GROUP, priv->key, tally);
  GError *error = NULL;
  if (!g_key_file_save_to_file (priv->key_file, priv->file_path, &error))
    {
      g_critical ("Failed to write to file. Error: %s.", error->message);
      g_error_free (error);
      return FALSE;
    }

  priv->tally = tally;
  priv->tally_cached = TRUE;
  return TRUE;
}

static gboolean
read_tally (EinsPersistentTally *self)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  if (priv->tally_cached)
    return TRUE;

  GError *error = NULL;
  if (!g_key_file_load_from_file (priv->key_file, priv->file_path,
                                  G_KEY_FILE_NONE, &error))
    {
      if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_error_free (error);
          return write_tally (self, 0);
        }

      goto handle_failed_read;
    }

  priv->tally = g_key_file_get_int64 (priv->key_file, GROUP, priv->key, &error);
  if (error != NULL)
    {
      if (g_error_matches (error, G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_KEY_NOT_FOUND))
        {
          g_error_free (error);
          return write_tally (self, 0);
        }

      goto handle_failed_read;
    }

  priv->tally_cached = TRUE;
  return TRUE;

handle_failed_read:
  g_warning ("Failed to read from file. Error: %s", error->message);
  g_error_free (error);
  return FALSE;
}

/*
 * Populates tally with the current value of the tally unless tally is NULL.
 * Returns TRUE if the tally was successfully retrieved and FALSE otheriwse.
 * If FALSE is returned, tally is not modified.
 */
gboolean
eins_persistent_tally_get_tally (EinsPersistentTally *self,
                                 gint64              *tally)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  if (!read_tally (self))
    return FALSE;

  if (tally != NULL)
    *tally = priv->tally;

  return TRUE;
}

/*
 * Adds the given delta to the persistent tally.
 * Returns TRUE on success and FALSE on failure.
 */
gboolean
eins_persistent_tally_add_to_tally (EinsPersistentTally *self,
                                    gint64               delta)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  if (!read_tally (self))
    return FALSE;

  return write_tally (self, priv->tally + delta);
}
