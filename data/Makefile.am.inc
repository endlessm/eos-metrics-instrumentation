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

edit = sed \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	-e 's|@instrumentationcachedir[@]|$(instrumentationcachedir)|g' \
	$(NULL)

tmpfilesddir = $(libdir)/tmpfiles.d/
tmpfilesd_DATA = data/eos-metrics-instrumentation.conf

EXTRA_DIST += \
	data/eos-metrics-instrumentation.service.in \
	data/eos-metrics-instrumentation.conf.in \
	$(NULL)

CLEANFILES += \
	data/eos-metrics-instrumentation.service \
	data/eos-metrics-instrumentation.conf \
	$(NULL)
