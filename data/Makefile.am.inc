# Defined as systemdsystemunitdir in ../configure.ac.
systemdsystemunit_DATA = data/eos-metrics-instrumentation.service

data/eos-metrics-instrumentation.service: data/eos-metrics-instrumentation.service.in
	$(AM_V_GEN)mkdir -p data && \
	rm -f $@ $@.tmp && \
	$(edit) $< >$@.tmp && \
	mv $@.tmp $@

edit = sed \
	-e 's|@libexecdir[@]|$(libexecdir)|g' \
	$(NULL)

EXTRA_DIST += data/eos-metrics-instrumentation.service.in

CLEANFILES += data/eos-metrics-instrumentation.service
