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
systemdsystemunit_DATA = \
	data/eos-metrics-instrumentation.path \
	data/eos-metrics-instrumentation.service

data/eos-metrics-instrumentation.%: data/eos-metrics-instrumentation.%.in
	$(AM_V_GEN)mkdir -p data && \
	rm -f $@ $@.tmp && \
	$(edit) $< >$@.tmp && \
	mv $@.tmp $@

edit = sed \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@instrumentationcachedir[@]|$(instrumentationcachedir)|g' \
	$(NULL)

tmpfilesddir = $(prefix)/lib/tmpfiles.d/
tmpfilesd_DATA = data/eos-metrics-instrumentation.conf

EXTRA_DIST += \
	data/eos-metrics-instrumentation.path.in \
	data/eos-metrics-instrumentation.service.in \
	data/eos-metrics-instrumentation.conf.in \
	$(NULL)

CLEANFILES += \
	data/eos-metrics-instrumentation.path \
	data/eos-metrics-instrumentation.service \
	data/eos-metrics-instrumentation.conf \
	$(NULL)
