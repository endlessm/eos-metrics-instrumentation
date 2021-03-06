/* Copyright 2017–2020 Endless Mobile, Inc. */

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

#ifndef EINS_LOCATION_LABEL_H
#define EINS_LOCATION_LABEL_H

#include <gio/gio.h>

GVariant *build_location_label_event (GKeyFile *kf);
gboolean record_location_label (gpointer unused);
GFileMonitor *location_file_monitor_new (void);

#endif /* EINS_LOCATION_LABEL_H */
