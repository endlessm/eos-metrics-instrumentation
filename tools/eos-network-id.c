/* Copyright 2017 Endless Mobile, Inc.
 *
 * This file is part of eos-metrics-instrumentation.
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

#include "eins-network-id.h"

int
main (int argc, char *argv[])
{
    guint32 network_id;

    if (!eins_network_id_get (&network_id))
      {
        g_printerr ("No network ID found. Check internet connection.\n");
        return 1;
      }

    g_print ("Network ID: %8x\n", network_id);

    return 0;
}

