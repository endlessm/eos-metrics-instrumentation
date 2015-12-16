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
} EinsPersistentTallyPrivate;

static void eins_persistent_tally_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (EinsPersistentTally, eins_persistent_tally,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (EinsPersistentTally)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, eins_persistent_tally_initable_iface_init))

/* The path to the key file containing the running tally. */
#define DEFAULT_FILE_PATH INSTRUMENTATION_CACHE_DIR "persistent-tallies"

#define GROUP "tallies"

#define MODE 02775

enum {
  PROP_0,
  PROP_FILE_PATH,
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

static gboolean
eins_persistent_tally_initable_init (GInitable    *initable,
                                     GCancellable *cancellable,
                                     GError      **error)
{
  EinsPersistentTally *self = EINS_PERSISTENT_TALLY (initable);
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  GError *local_error = NULL;
  if (g_key_file_load_from_file (priv->key_file, priv->file_path,
                                 G_KEY_FILE_NONE, &local_error))
    return TRUE;

  if (g_error_matches (local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    {
      g_error_free (local_error);
      return TRUE;
    }

  g_propagate_error (error, local_error);
  return FALSE;
}

static void
eins_persistent_tally_initable_iface_init (GInitableIface *iface)
{
  iface->init = eins_persistent_tally_initable_init;
}

EinsPersistentTally *
eins_persistent_tally_new (GError **error)
{
  return g_initable_new (EINS_TYPE_PERSISTENT_TALLY,
                         NULL /* GCancellable*/,
                         error,
                         NULL);
}

EinsPersistentTally *
eins_persistent_tally_new_full (const gchar *file_path,
                                GError     **error)
{
  return g_initable_new (EINS_TYPE_PERSISTENT_TALLY,
                         NULL /* GCancellable */,
                         error,
                         "file-path", file_path,
                         NULL);
}

/*
 * Populates tally with the current value of the tally associated with the given
 * key. Returns TRUE if the tally was successfully retrieved and FALSE
 * otherwise.
 */
gboolean
eins_persistent_tally_get_tally (EinsPersistentTally *self,
                                 const gchar         *key,
                                 gint64              *tally)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  GError *error = NULL;
  *tally = g_key_file_get_int64 (priv->key_file, GROUP, key, &error);
  if (error != NULL)
    {
      if (g_error_matches (error, G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
          g_error_matches (error, G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_GROUP_NOT_FOUND))
        {
          g_error_free (error);
          return TRUE;
        }

      g_warning ("Could not get tally for key %s. Error: %s.", key,
                 error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

/*
 * Adds the given delta to the persistent tally associated with the given key.
 * Returns TRUE on success and FALSE on failure.
 */
gboolean
eins_persistent_tally_add_to_tally (EinsPersistentTally *self,
                                    const gchar         *key,
                                    gint64               delta)
{
  EinsPersistentTallyPrivate *priv =
    eins_persistent_tally_get_instance_private (self);

  gint64 tally;
  if (!eins_persistent_tally_get_tally (self, key, &tally))
    return FALSE;

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

  g_key_file_set_int64 (priv->key_file, GROUP, key, tally + delta);

  GError *error = NULL;
  if (!g_key_file_save_to_file (priv->key_file, priv->file_path, &error))
    {
      g_critical ("Failed to write to file. Error: %s.", error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}
