# Copyright 2014 Endless Mobile, Inc.

libexec_PROGRAMS = eos-metrics-instrumentation

eos_metrics_instrumentation_SOURCES = \
	src/eos-metrics-instrumentation.c \
	src/eins-persistent-tally.h \
	src/eins-persistent-tally.c \
	$(NULL)

# Pre-processor flags
eos_metrics_instrumentation_CPPFLAGS = \
	$(EOS_INSTRUMENTATION_CFLAGS) \
	-DINSTRUMENTATION_CACHE_DIR="\"$(instrumentationcachedir)\"" \
	$(NULL)

# Libraries to link against
eos_metrics_instrumentation_LDADD = $(EOS_INSTRUMENTATION_LIBS)
