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

# Defined as systemdsystemunitdir in ../configure.ac.
systemdsystemunit_DATA = data/eos-metrics-instrumentation.service

data/eos-metrics-instrumentation.service: data/eos-metrics-instrumentation.service.in
	$(AM_V_GEN)mkdir -p data && \
	rm -f $@ $@.tmp && \
	$(edit) $< >$@.tmp && \
	mv $@.tmp $@

data/eos-metrics-instrumentation.conf: data/eos-metrics-instrumentation.conf.in
	$(AM_V_GEN)$(MKDIR_P) data && \
	rm -f $@ $@.tmp && \
	$(edit) $< >$@.tmp && \
	mv $@.tmp $@

data/90-eos-crash-metrics.conf: data/90-eos-crash-metrics.conf.in
	$(AM_V_GEN)$(MKDIR_P) data && \
	rm -f $@ $@.tmp && \
	$(edit) $< >$@.tmp && \
	mv $@.tmp $@

edit = sed \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@instrumentationcachedir[@]|$(instrumentationcachedir)|g' \
	$(NULL)

tmpfilesddir = $(prefix)/lib/tmpfiles.d/
tmpfilesd_DATA = data/eos-metrics-instrumentation.conf

sysctldir = $(prefix)/lib/sysctl.d/
sysctl_DATA = data/90-eos-crash-metrics.conf

EXTRA_DIST += \
	data/90-eos-crash-metrics.conf.in \
	data/eos-metrics-instrumentation.service.in \
	data/eos-metrics-instrumentation.conf.in \
	$(NULL)

CLEANFILES += \
	data/90-eos-crash-metrics.conf \
	data/eos-metrics-instrumentation.service \
	data/eos-metrics-instrumentation.conf \
	$(NULL)
