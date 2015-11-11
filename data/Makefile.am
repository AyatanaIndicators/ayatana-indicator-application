
SUBDIRS = upstart

upstart_jobsdir = $(datadir)/upstart/sessions/
upstart_jobs_DATA = ayatana-indicator-application.conf

%.conf: %.conf.in
	$(AM_V_GEN) sed -e "s|\@pkglibexecdir\@|$(pkglibexecdir)|" $< > $@

xdg_autostartdir = /etc/xdg/autostart
xdg_autostart_DATA = ayatana-indicator-application.desktop

%.desktop: %.desktop.in
	$(AM_V_GEN) sed -e "s|\@pkglibexecdir\@|$(pkglibexecdir)|" $< > $@

overridedir = $(pkgdatadir)
override_DATA = ordering-override.keyfile

EXTRA_DIST = \
	ayatana-indicator-application.desktop.in \
	ayatana-indicator-application.conf.in \
	ordering-override.keyfile

CLEANFILES = \
	ayatana-indicator-application.conf \
	ayatana-indicator-application.desktop

