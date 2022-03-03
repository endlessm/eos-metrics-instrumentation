## Copyright 2014, 2015, 2017 Endless Mobile, Inc.

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

nodist_bin_SCRIPTS = tools/eos-label-location

do_edit = sed \
	-e 's|[@]sysconfdir[@]|$(sysconfdir)|g'

tools/eos-label-location: tools/eos-label-location.in Makefile
	@$(MKDIR_P) $(dir $@)
	$(AM_V_GEN) $(do_edit) $< >$@.tmp && mv $@{.tmp,}

EXTRA_DIST += \
	tools/eos-label-location.in \
	$(NULL)

CLEANFILES += \
	tools/eos-label-location \
	$(NULL)
