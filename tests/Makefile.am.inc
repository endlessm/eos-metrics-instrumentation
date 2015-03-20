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
	tests/test-persistent-tally \
	$(NULL)

TESTS = \
	tests/test-persistent-tally \
	tests/test-location.py \
	$(NULL)
TEST_EXTENSIONS = .py
PY_LOG_COMPILER = python

TEST_FLAGS = \
	@EOS_INSTRUMENTATION_CFLAGS@ \
	-I$(top_srcdir)/src \
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

EXTRA_DIST += tests/test-location.py
