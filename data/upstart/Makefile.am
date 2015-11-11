
xdg_autostartdir = $(pkgdatadir)/upstart/xdg/autostart
xdg_autostart_DATA = ayatana-indicator-application.desktop

%.desktop: %.desktop.in
	sed -e "s|\@libexecdir\@|$(pkglibexecdir)|" $< > $@

EXTRA_DIST = \
	ayatana-indicator-application.desktop.in
