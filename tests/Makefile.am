
check_PROGRAMS = \
	test-approver

TESTS =
DISTCLEANFILES = $(TESTS)

EXTRA_DIST = run-xvfb.sh

DBUS_RUNNER=dbus-test-runner --dbus-config /usr/share/dbus-test-runner/session.conf

#########################################
##  test-approver
#########################################

test_approver_SOURCES = \
	$(top_srcdir)/src/gen-notification-approver.xml.c \
	test-approver.c

test_approver_CFLAGS = \
	$(INDICATOR_CFLAGS) \
	$(APPINDICATOR_CFLAGS) \
	-Wall -Werror \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src

test_approver_LDADD = \
	$(INDICATOR_LIBS) \
	$(APPINDICATOR_LIBS)

test-approver-tester: test-approver Makefile.am
	@echo "#!/bin/bash" > $@
	@echo export INDICATOR_SERVICE_SHUTDOWN_TIMEOUT=1000 >> $@
	@echo . $(srcdir)/run-xvfb.sh >> $@
	@echo $(DBUS_RUNNER) --task $(builddir)/test-approver --task-name Approver --task $(top_builddir)/src/indicator-application-service --task-name Service --ignore-return >> $@
	@chmod +x $@

TESTS += test-approver-tester


