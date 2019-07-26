/* Copyright 2019 Endless Mobile, Inc. */

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

#include <glib.h>

#include "eins-location.h"

gint
main (gint                argc,
      const gchar * const argv[])
{
  g_autoptr(GMainLoop) main_loop = g_main_loop_new (NULL, TRUE);
  g_idle_add ((GSourceFunc) record_location_metric, "solutions-fake-image");
  g_main_loop_run (main_loop);
  return EXIT_SUCCESS;
}
