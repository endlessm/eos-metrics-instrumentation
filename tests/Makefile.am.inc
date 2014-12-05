noinst_PROGRAMS += \
	tests/test-persistent-tally \
	$(NULL)

TESTS = \
	tests/test-persistent-tally \
	$(NULL)

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
