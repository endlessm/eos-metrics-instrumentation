systemdsystemunit_DATA = eos-metrics-instrumentation.service

eos-metrics-instrumentation.service: eos-metrics-instrumentation.service.in
	$(edit) $< >$@

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

EXTRA_DIST = eos-metrics-instrumentation.service.in

CLEANFILES = $(systemdsystemunit_DATA)
