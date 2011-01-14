if USE_GTK3
VER=3
else
VER=
endif

CLEANFILES =
DISTCLEANFILES =
BUILT_SOURCES =
EXTRA_DIST = 

include $(top_srcdir)/Makefile.am.enum
include $(top_srcdir)/Makefile.am.marshal

##################################
# Indicator
##################################

applicationlibdir = $(INDICATORDIR)
applicationlib_LTLIBRARIES = libapplication.la
libapplication_la_SOURCES = \
	application-service-marshal.c \
	dbus-shared.h \
	indicator-application.c
libapplication_la_CFLAGS = $(INDICATOR_CFLAGS) \
	-Wall \
	-Wl,-Bsymbolic-functions \
	-Wl,-z,defs \
	-Wl,--as-needed \
	-Werror \
	-DG_LOG_DOMAIN=\"Indicator-Application\"
libapplication_la_LIBADD = $(INDICATOR_LIBS)
libapplication_la_LDFLAGS = -module -avoid-version

##################################
# Service
##################################

libexec_PROGRAMS = indicator-application-service

BUILT_SOURCES += \
	application-service-marshal.h \
	application-service-marshal.c \
	notification-watcher-server.h

indicator_application_service_SOURCES = \
	application-service.c \
	application-service-appstore.h \
	application-service-appstore.c \
	application-service-marshal.c \
	application-service-watcher.h \
	application-service-watcher.c \
	gen-application-service.xml.c \
	app-indicator-enum-types.c \
	dbus-shared.h \
	generate-id.h \
	generate-id.c

indicator_application_service_CFLAGS = \
	$(INDICATOR_CFLAGS) \
	-I$(APP_INDICATOR_INCLUDE) \
	-DDATADIR="\"$(pkgdatadir)\"" \
	-Wall -Werror \
	-DG_LOG_DOMAIN=\"indicator-application-service\"

indicator_application_service_LDADD = \
	$(INDICATOR_LIBS)

glib_marshal_list = application-service-marshal.list
glib_marshal_prefix = _application_service_marshal

glib_enum_h = app-indicator-enum-types.h
glib_enum_c = app-indicator-enum-types.c
glib_enum_headers = $(wildcard $(APP_INDICATOR_INCLUDE)/libappindicator/*.h)

DISTCLEANFILES += app-indicator-enum-types.c

##################################
# DBus Specs
##################################

GDBUS_SPECS = \
	application-service.xml \
	notification-approver.xml

DBUS_SPECS = \
	notification-watcher.xml

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
	gen-application-service.xml.c \
	gen-application-service.xml.h \
	gen-notification-approver.xml.c \
	gen-notification-approver.xml.h

CLEANFILES += $(BUILT_SOURCES)

EXTRA_DIST += $(DBUS_SPECS) $(GDBUS_SPECS)
