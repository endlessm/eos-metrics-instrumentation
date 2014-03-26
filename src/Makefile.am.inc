# Copyright 2014 Endless Mobile, Inc.

libexec_PROGRAMS = eos-metrics-instrumentation

eos_metrics_instrumentation_SOURCES = src/eos-metrics-instrumentation.c

# Pre-processor flags
AM_CPPFLAGS = $(EOS_INSTRUMENTATION_CFLAGS)

# Libraries to link against
eos_metrics_instrumentation_LDADD = $(EOS_INSTRUMENTATION_LIBS)
