/* Copyright 2014 Endless Mobile, Inc. */

#ifndef EINS_PERSISTENT_TALLY_H
#define EINS_PERSISTENT_TALLY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define EINS_TYPE_PERSISTENT_TALLY eins_persistent_tally_get_type()

#define EINS_PERSISTENT_TALLY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  EINS_TYPE_PERSISTENT_TALLY, EinsPersistentTally))

#define EINS_PERSISTENT_TALLY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  EINS_PERSISTENT_TALLY, EinsPersistentTallyClass))

#define EINS_IS_PERSISTENT_TALLY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  EINS_TYPE_PERSISTENT_TALLY))

#define EINS_IS_PERSISTENT_TALLY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  EINS_TYPE_PERSISTENT_TALLY))

#define EINS_PERSISTENT_TALLY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  EINS_TYPE_PERSISTENT_TALLY, EinsPersistentTallyClass))

typedef struct _EinsPersistentTally EinsPersistentTally;

typedef struct _EinsPersistentTallyClass EinsPersistentTallyClass;

struct _EinsPersistentTally
{
  /*< private >*/
  GObject parent;
};

struct _EinsPersistentTallyClass
{
  /*< private >*/
  GObjectClass parent_class;
};

GType                eins_persistent_tally_get_type     (void) G_GNUC_CONST;

EinsPersistentTally *eins_persistent_tally_new          (const gchar         *key);

EinsPersistentTally *eins_persistent_tally_new_full     (const gchar         *file_path,
                                                         const gchar         *key);

gboolean             eins_persistent_tally_get_tally    (EinsPersistentTally *self,
                                                         gint64              *tally);

gboolean             eins_persistent_tally_add_to_tally (EinsPersistentTally *self,
                                                         gint64               delta);

G_END_DECLS

#endif /* EINS__H */
