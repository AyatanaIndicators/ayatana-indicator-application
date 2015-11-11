CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =
EXTRA_DIST = 

include $(top_srcdir)/Makefile.am.marshal

##################################
# Indicator
##################################

applicationlibdir = $(INDICATORDIR)
applicationlib_LTLIBRARIES = libayatana-application.la
libayatana_application_la_SOURCES = \
	ayatana-application-service-marshal.c \
	dbus-shared.h \
	indicator-application.c
libayatana_application_la_CFLAGS = $(INDICATOR_CFLAGS) \
	$(COVERAGE_CFLAGS) \
	-Wall \
	-Wl,-Bsymbolic-functions \
	-Wl,-z,defs \
	-Wl,--as-needed \
	-Werror -Wno-error=deprecated-declarations \
	-DG_LOG_DOMAIN=\"Ayatana-Indicator-Application\"
libayatana_application_la_LIBADD = $(INDICATOR_LIBS) 
libayatana_application_la_LDFLAGS = $(COVERAGE_LDFLAGS) \
	-module -avoid-version

##################################
# Service
##################################

pkglibexec_PROGRAMS = ayatana-indicator-application-service

BUILT_SOURCES += \
	ayatana-application-service-marshal.h \
	ayatana-application-service-marshal.c \
	ayatana-notification-watcher-server.h

ayatana_indicator_application_service_SOURCES = \
	application-service.c \
	application-service-appstore.h \
	application-service-appstore.c \
	ayatana-application-service-marshal.c \
	application-service-watcher.h \
	application-service-watcher.c \
	gen-ayatana-application-service.xml.c \
	dbus-shared.h \
	generate-id.h \
	generate-id.c

ayatana_indicator_application_service_CFLAGS = \
	$(INDICATOR_CFLAGS) \
	$(APPINDICATOR_CFLAGS) \
	$(COVERAGE_CFLAGS) \
	-DDATADIR="\"$(pkgdatadir)\"" \
	-Wall -Werror -Wno-error=deprecated-declarations \
	-DG_LOG_DOMAIN=\"ayatana-indicator-application-service\"

ayatana_indicator_application_service_LDADD = \
	$(INDICATOR_LIBS) \
	$(APPINDICATOR_LIBS)

ayatana_indicator_application_service_LDFLAGS = \
	$(COVERAGE_LDFLAGS)

glib_marshal_list = ayatana-application-service-marshal.list
glib_marshal_prefix = _ayatana_application_service_marshal

##################################
# DBus Specs
##################################

GDBUS_SPECS = \
	ayatana-application-service.xml

DBUS_SPECS = \
	ayatana-notification-watcher.xml

%-client.h: %.xml
	dbus-binding-tool \
		--prefix=_$(notdir $(subst -,_,$(<:.xml=)))_client \
		--mode=glib-client \
		--output=$@ \
		$<

%-server.h: %.xml
	dbus-binding-tool \
		--prefix=_$(notdir $(subst -,_,$(<:.xml=)))_server \
		--mode=glib-server \
		--output=$@ \
		$<

gen-%.xml.c: %.xml
	@echo "Building $@ from $<"
	@echo "const char * _$(notdir $(subst -,_,$(subst .,_,$(basename $<)))) = " > $@
	@sed -e "s:\":\\\\\":g" -e s:^:\": -e s:\$$:\\\\n\": $< >> $@
	@echo ";" >> $@

gen-%.xml.h: %.xml
	@echo "Building $@ from $<"
	@echo "extern const char * _$(notdir $(subst -,_,$(subst .,_,$(basename $<))));" > $@

BUILT_SOURCES += \
	$(DBUS_SPECS:.xml=-client.h) \
	$(DBUS_SPECS:.xml=-server.h) \
	gen-ayatana-application-service.xml.c \
	gen-ayatana-application-service.xml.h

CLEANFILES += $(BUILT_SOURCES)

EXTRA_DIST += $(DBUS_SPECS) $(GDBUS_SPECS)
