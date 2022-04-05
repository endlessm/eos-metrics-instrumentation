/* Copyright 2018 Endless OS Foundation LLC. */

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
#pragma once

#include <glib.h>
#include <gio/gio.h>

void eins_hwinfo_start (void);

/* For tests */
typedef struct _DiskSpaceType {
  guint32 total;
  guint32 used;
  guint32 free;
} DiskSpaceType;

gboolean eins_hwinfo_get_disk_space_for_partition (GFile          *file,
                                                   DiskSpaceType  *diskspace,
                                                   GError        **error);

guint32 eins_hwinfo_get_ram_size (void);

GVariant *eins_hwinfo_get_cpu_info (void);
GVariant *eins_hwinfo_parse_lscpu_json (const gchar *json_data,
                                        gssize       json_size);

GVariant *eins_hwinfo_get_computer_hwinfo (void);
