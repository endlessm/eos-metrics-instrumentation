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

noinst_PROGRAMS += \
	tests/test-hwinfo \
	tests/test-location-label \
	tests/test-persistent-tally \
	$(NULL)

TESTS = \
	tests/test-hwinfo \
	tests/test-persistent-tally \
	tests/test-location-label \
	run_coverage.coverage \
	$(NULL)
TEST_EXTENSIONS = .py .coverage
PY_LOG_COMPILER = python3

TEST_FLAGS = \
	@EOS_INSTRUMENTATION_CFLAGS@ \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	-DINSTRUMENTATION_CACHE_DIR="\"$(instrumentationcachedir)\"" \
	$(NULL)
TEST_LIBS = @EOS_INSTRUMENTATION_LIBS@

tests_test_persistent_tally_SOURCES = \
	src/eins-persistent-tally.h \
	src/eins-persistent-tally.c \
	tests/test-persistent-tally.c \
	$(NULL)

tests_test_persistent_tally_CPPFLAGS = $(TEST_FLAGS)

tests_test_persistent_tally_LDADD = $(TEST_LIBS)

tests_test_hwinfo_SOURCES = \
	src/eins-hwinfo.h \
	src/eins-hwinfo.c \
	tests/test-hwinfo.c \
	$(NULL)

tests_test_hwinfo_CPPFLAGS = $(TEST_FLAGS)

tests_test_hwinfo_LDADD = $(TEST_LIBS)

tests_test_location_SOURCES = \
	src/eins-location.c src/eins-location.h \
	src/geoclue.c \
	$(NULL)
tests_test_location_CPPFLAGS = $(TEST_FLAGS)
tests_test_location_LDADD = $(TEST_LIBS)

tests_test_location_label_SOURCES = \
	src/eins-location-label.h \
	src/eins-location-label.c \
	tests/test-location-label.c \
	$(NULL)

tests_test_location_label_CPPFLAGS = $(TEST_FLAGS) \
	-DSYSCONFDIR="\"$(sysconfdir)\"" \
	$(NULL)

tests_test_location_label_LDADD = $(TEST_LIBS)

clean-local:: clean-coverage
