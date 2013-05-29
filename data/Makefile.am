
upstart_jobsdir = $(datadir)/upstart/sessions/
upstart_jobs_DATA = indicator-application.conf

%.conf: %.conf.in
	sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

overridedir = $(pkgdatadir)
override_DATA = ordering-override.keyfile

EXTRA_DIST = \
	indicator-application.conf.in \
	ordering-override.keyfile

CLEANFILES = \
	indicator-application.conf

