dnl Process this file with autoconf to produce configure

dnl Copyright 2014, 2015 Endless Mobile, Inc.

dnl This file is part of eos-metrics-instrumentation.
dnl
dnl eos-metrics-instrumentation is free software: you can redistribute it and/or
dnl modify it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 2 of the License, or (at your
dnl option) any later version.
dnl
dnl eos-metrics-instrumentation is distributed in the hope that it will be
dnl useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
dnl Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with eos-metrics-instrumentation.  If not, see
dnl <http://www.gnu.org/licenses/>.

dnl ------------
dnl configure.ac
dnl ------------
dnl Please keep this file well-commented. Autotools involve a lot of magical
dnl incantations, and it is easy to mess things up if you don't know what you
dnl are doing.

# Version numbers
# ---------------
# Name and version for pkg-config; increment when introducing breaking changes.
# This version number is also the major version number.
m4_define([_EOS_INSTRUMENTATION_MAJOR_VERSION_MACRO], [0])

# Minor and micro versions: increment micro version when making a release. Minor
# version is even for a stable release and odd for a development release.
# When making any release, if the API changes, set the interface age to 0.
m4_define([_EOS_INSTRUMENTATION_MINOR_VERSION_MACRO], [0])
m4_define([_EOS_INSTRUMENTATION_MICRO_VERSION_MACRO], [0])

# Full version, for use in AC_INIT
m4_define([_EOS_INSTRUMENTATION_VERSION_MACRO],
	[_EOS_INSTRUMENTATION_MAJOR_VERSION_MACRO._EOS_INSTRUMENTATION_MINOR_VERSION_MACRO._EOS_INSTRUMENTATION_MICRO_VERSION_MACRO])

# Initialization
# --------------
# Initialize Autoconf: package name, version, bug report address, tarball name,
# website
AC_INIT([EOS Metrics Instrumentation Daemons], [_EOS_INSTRUMENTATION_VERSION_MACRO],
	[], [eos-metrics-instrumentation], [http://endlessm.com])
# Verify that the source directory can be found
AC_CONFIG_SRCDIR([src/eos-metrics-instrumentation.c])
# Initialize Automake: enable all warnings and do not insist on GNU standards.
# no-portability suppresses warnings about syntax specific to GNU make.
# parallel-tests specifies that we use the new parallel-running test harness.
# Unlike serial-tests, this option is accepted by Automake 1.11.
# subdir-objects ensures forward compatibility with Automake 2.0 and can be
# removed when this behavior becomes the default.
# tar-ustar is required because there may be files whose entire paths exceed
# 99 characters.
AM_INIT_AUTOMAKE([-Wall -Wno-portability foreign 1.11 parallel-tests
    subdir-objects tar-ustar])
# Avoid spewing garbage over the terminal ('make V=1' to see the garbage)
AM_SILENT_RULES([yes])
# Keep Autotools macros local to this source tree
AC_CONFIG_MACRO_DIR([m4])

AC_CACHE_SAVE

# Variables to define
# -------------------
# Version numbers and macros
EOS_INSTRUMENTATION_VERSION=_EOS_INSTRUMENTATION_VERSION_MACRO
AC_SUBST(EOS_INSTRUMENTATION_VERSION)
EOS_INSTRUMENTATION_MAJOR_VERSION=_EOS_INSTRUMENTATION_MAJOR_VERSION_MACRO
AC_SUBST(EOS_INSTRUMENTATION_MAJOR_VERSION)
AC_DEFINE([EOS_INSTRUMENTATION_MAJOR_VERSION], [_EOS_INSTRUMENTATION_MAJOR_VERSION_MACRO], [Major version])
EOS_INSTRUMENTATION_MINOR_VERSION=_EOS_INSTRUMENTATION_MINOR_VERSION_MACRO
EOS_INSTRUMENTATION_MICRO_VERSION=_EOS_INSTRUMENTATION_MICRO_VERSION_MACRO
AC_DEFINE([EOS_INSTRUMENTATION_MINOR_VERSION], [_EOS_INSTRUMENTATION_MINOR_VERSION_MACRO], [Minor version])
AC_DEFINE([EOS_INSTRUMENTATION_MICRO_VERSION], [_EOS_INSTRUMENTATION_MICRO_VERSION_MACRO], [Micro version])
AC_DEFINE([EOS_INSTRUMENTATION_VERSION], [_EOS_INSTRUMENTATION_VERSION_MACRO], [Full version number])

# Required versions of libraries
# Update these whenever you use a function that requires a certain API version
EOS_METRICS_REQUIREMENT="eosmetrics-0 >= 0.3.0"
FLATPAK_REQUIREMENT="flatpak"
GIO_REQUIREMENT="gio-2.0"
GLIB_REQUIREMENT="glib-2.0 >= 2.63.1"
JSON_GLIB_REQUIREMENT="json-glib-1.0"
LIBGTOP_REQUIREMENT="libgtop-2.0"
OSTREE_REQUIREMENT="ostree-1"

# Required build tools
# --------------------
# Make sure we can create directory hierarchies
AC_PROG_MKDIR_P
# C compiler
AC_PROG_CC
# Make sure the C compiler supports per-target CFLAGS
AM_PROG_CC_C_O
# Intializes PKG_CONFIG variable so that systemdsystemunitdir can be found.
PKG_PROG_PKG_CONFIG

AC_CACHE_SAVE

# Check that the compiler supports C99, and enable it in our CFLAGS
AC_PROG_CC_C99

# Configure options
# -----------------
# --enable-strict-flags: Compile with strict compiler flags. Done automatically
# during 'make distcheck'.
AC_ARG_ENABLE([strict-flags],
    [AS_HELP_STRING([--enable-strict-flags=@<:@no/yes/error@:>@],
        [Use strict compiler flags @<:@default=no@:>@])],
    [],
    [enable_strict_flags=no])
# Emmanuele's list of flags
STRICT_COMPILER_FLAGS="$STRICT_COMPILER_FLAGS
    -Wall
    -Wcast-align
    -Wuninitialized
    -Wno-strict-aliasing
    -Werror=pointer-arith
    -Werror=missing-declarations
    -Werror=redundant-decls
    -Werror=empty-body
    -Werror=format
    -Werror=format-security
    -Werror=format-nonliteral
    -Werror=init-self"
AS_CASE([$enable_strict_flags],
    [yes],
        [AS_COMPILER_FLAGS([STRICT_CFLAGS], [$STRICT_COMPILER_FLAGS])],
    [no],
        [],
        [error],
        [
            STRICT_COMPILER_FLAGS="$STRICT_COMPILER_FLAGS -Werror"
            AS_COMPILER_FLAGS([STRICT_CFLAGS], [$STRICT_COMPILER_FLAGS])
        ],
    [AC_MSG_ERROR([Invalid option for --enable-strict-flags])])
dnl Strip leading spaces
STRICT_CFLAGS=${STRICT_CFLAGS#*  }
AC_SUBST(STRICT_CFLAGS)

systemdsystemunitdir="$($PKG_CONFIG systemd --variable=systemdsystemunitdir)"
dnl Allow overriding systemdsystemunitdir during distcheck in order to trick
dnl Automake into allowing an install outside of $prefix
AC_ARG_WITH([systemdsystemunitdir],
    [AS_HELP_STRING([--with-systemdsystemunitdir=PATH], [directory for systemd service files])],
    [systemdsystemunitdir="$withval"])
AC_SUBST([systemdsystemunitdir])

# Required libraries
# ------------------
PKG_CHECK_MODULES([EOS_INSTRUMENTATION], [
    $EOS_METRICS_REQUIREMENT
    $FLATPAK_REQUIREMENT
    $GLIB_REQUIREMENT
    $GIO_REQUIREMENT
    $JSON_GLIB_REQUIREMENT
    $LIBGTOP_REQUIREMENT
    $OSTREE_REQUIREMENT
])

# Code coverage
# -------------
EOS_COVERAGE_REPORT([c])

AC_CACHE_SAVE

# Output
# ------
# List files here that the configure script should output
AC_CONFIG_FILES([
	Makefile
])
AC_CONFIG_HEADERS([config.h])  dnl Header with system-dependent #defines
AC_OUTPUT
