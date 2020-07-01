## Copyright 2014, 2015 Endless Mobile, Inc.

## This file is part of eos-metrics-instrumentation.
##
## eos-metrics-instrumentation is free software: you can redistribute it and/or
## modify it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 2 of the License, or (at your
## option) any later version.
##
## eos-metrics-instrumentation is distributed in the hope that it will be
## useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
## Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with eos-metrics-instrumentation.  If not, see
## <http://www.gnu.org/licenses/>.

libexec_PROGRAMS = \
	src/eos-metrics-instrumentation \
	src/eos-crash-metrics \
	$(NULL)

src/geoclue.c: src/geoclue.h
src/geoclue.h: $(GEOCLUE_DBUS_INTERFACE_XML)
	$(AM_V_GEN)$(GDBUS_CODEGEN) --interface-prefix org.freedesktop.GeoClue2 \
		--generate-c-code src/geoclue --c-namespace Geoclue $<
BUILT_SOURCES += src/geoclue.c src/geoclue.h

src_eos_metrics_instrumentation_SOURCES = \
	src/eos-metrics-instrumentation.c \
	src/eins-hwinfo.h \
	src/eins-hwinfo.c \
	src/eins-location.h \
	src/eins-location.c \
	src/eins-location-label.h \
	src/eins-location-label.c \
	src/eins-network-id.h \
	src/eins-network-id.c \
	src/eins-persistent-tally.h \
	src/eins-persistent-tally.c \
	src/geoclue.h \
	src/geoclue.c \
	$(NULL)

# Pre-processor flags
src_eos_metrics_instrumentation_CPPFLAGS = \
	-I$(top_builddir)/src \
	$(EOS_INSTRUMENTATION_CFLAGS) \
	-DINSTRUMENTATION_CACHE_DIR="\"$(instrumentationcachedir)\"" \
	-DSYSCONFDIR="\"$(sysconfdir)\"" \
	$(NULL)

# Libraries to link against
src_eos_metrics_instrumentation_LDADD = $(EOS_INSTRUMENTATION_LIBS)

src_eos_crash_metrics_SOURCES = \
	src/eos-crash-metrics.c \
	$(NULL)


src_eos_crash_metrics_CPPFLAGS = \
	$(EOS_INSTRUMENTATION_CFLAGS) \
	$(NULL)

src_eos_crash_metrics_LDADD = $(EOS_INSTRUMENTATION_LIBS)
