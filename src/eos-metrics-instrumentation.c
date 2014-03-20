/* Copyright 2014 Endless Mobile, Inc. */

#include <glib.h>
#include <stdio.h>
#include "eos-metrics-instrumentation.h"

static gboolean
meet_and_greet (gpointer data)
{
    printf ("Hello World!\n");
    return TRUE;
}

int
main(int argc, const char const *argv[])
{
    GMainLoop *main_loop = g_main_loop_new (NULL, TRUE);
    g_timeout_add_seconds (2, meet_and_greet, NULL);
    g_main_loop_run (main_loop);
    g_main_loop_unref (main_loop);
    return 0;
}
