## Process this file with automake to produce Makefile.in

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

## -----------
## Makefile.am
## -----------
## Please keep this file well-commented.

ACLOCAL_AMFLAGS = -I m4

# Extra files to distribute in the tarball
EXTRA_DIST = README.md

# Compiler flags
AM_CFLAGS = @STRICT_CFLAGS@

# Make sure to use strict compiler flags when doing 'make distcheck', and
# trick Automake into not installing something outside of $(prefix)
AM_DISTCHECK_CONFIGURE_FLAGS = \
        --enable-strict-flags \
        --with-systemdsystemunitdir='$${libdir}/systemd/system' \
        $(NULL)

# Generated files that 'make clean' should erase
CLEANFILES =
DISTCLEANFILES =

# Binaries that should be produced but not installed.
noinst_PROGRAMS =

# Generated sources
BUILT_SOURCES =

instrumentationcachedir=$(localstatedir)/cache/eos-metrics-instrumentation/

# # # CODE COVERAGE # # #

EOS_COVERAGE_BLACKLIST_PATTERNS = "*/tests/*"

@EOS_COVERAGE_RULES@

AM_CFLAGS += @EOS_C_COVERAGE_CFLAGS@
AM_LDFLAGS = @EOS_C_COVERAGE_LDFLAGS@

# # # INSTRUMENTATION # # #

# Instrumentation
include $(top_srcdir)/src/Makefile.am.inc

# Configuration
include $(top_srcdir)/data/Makefile.am.inc

# Tests
include $(top_srcdir)/tests/Makefile.am.inc
