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
