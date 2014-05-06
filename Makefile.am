## Process this file with automake to produce Makefile.in

# Copyright 2014 Endless Mobile, Inc.

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

# # # INSTRUMENTATION # # #

# Instrumentation
include $(top_srcdir)/src/Makefile.am.inc

# Daemon Configuration
include $(top_srcdir)/data/Makefile.am.inc
