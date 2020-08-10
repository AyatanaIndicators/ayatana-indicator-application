NULL =

xdg_autostartdir = /etc/xdg/autostart
xdg_autostart_DATA = ayatana-indicator-application.desktop

%.desktop: %.desktop.in
	$(AM_V_GEN) sed -e "s|\@pkglibexecdir\@|$(pkglibexecdir)|" $< > $@

overridedir = $(pkgdatadir)
override_DATA = ordering-override.keyfile

#if defined(HAVE_SYSTEMD)
%.service: %.service.in
	$(AM_V_GEN) sed -e "s|\@pkglibexecdir\@|$(pkglibexecdir)|" $< > $@

systemddir = $(SYSTEMD_USERDIR)
systemd_DATA = ayatana-indicator-application.service
#endif

EXTRA_DIST = \
	ayatana-indicator-application.desktop.in \
	ayatana-indicator-application.service.in \
	ordering-override.keyfile \
	$(NULL)

CLEANFILES = \
	ayatana-indicator-application.desktop \
	ayatana-indicator-application.service \
	$(NULL)
