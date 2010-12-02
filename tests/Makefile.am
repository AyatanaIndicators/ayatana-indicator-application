
check_PROGRAMS = \
	test-approver

TESTS =
DISTCLEANFILES = $(TESTS)

EXTRA_DIST = run-xvfb.sh

#########################################
##  test-approver
#########################################

test_approver_SOURCES = \
	test-approver.c

test_approver_CFLAGS = \
	$(INDICATOR_CFLAGS) \
	$(TEST_CFLAGS) \
	-Wall -Werror \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src

test_approver_LDADD = \
	$(INDICATOR_LIBS) \
	$(TEST_LIBS) \
	$(top_builddir)/src/libappindicator.la

test-approver-tester: test-approver Makefile.am
	@echo "#!/bin/bash" > $@
	@echo export INDICATOR_SERVICE_SHUTDOWN_TIMEOUT=1000 >> $@
	@echo . $(srcdir)/run-xvfb.sh >> $@
	@echo $(DBUS_RUNNER) --task $(builddir)/test-approver --task-name Approver --task $(top_builddir)/src/indicator-application-service --task-name Service --ignore-return >> $@
	@chmod +x $@

TESTS += test-approver-tester


