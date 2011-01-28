
AC_INIT(indicator-application, 0.2.92, ted@canonical.com)
AC_COPYRIGHT([Copyright 2009, 2010 Canonical])

AC_PREREQ(2.53)

AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(indicator-application, 0.2.92)

AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_C_O
AC_STDC_HEADERS
AC_PROG_LIBTOOL

AC_SUBST(VERSION)
AC_CONFIG_MACRO_DIR([m4])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PATH_PROG([GLIB_MKENUMS], [glib-mkenums])
AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])

PKG_PROG_PKG_CONFIG

###########################
# Dependencies
###########################

GTK_REQUIRED_VERSION=2.18
GTK3_REQUIRED_VERSION=2.91
GIO_REQUIRED_VERSION=2.26
INDICATOR_REQUIRED_VERSION=0.3.5
DBUSMENUGTK_REQUIRED_VERSION=0.3.91
JSON_GLIB_REQUIRED_VERSION=0.7.6
DBUS_GLIB_REQUIRED_VERSION=0.82

AC_ARG_WITH([gtk],
  [AS_HELP_STRING([--with-gtk],
    [Which version of gtk to use @<:@default=2@:>@])],
  [],
  [with_gtk=2])
AS_IF([test "x$with_gtk" = x3],
        [PKG_CHECK_MODULES(INDICATOR,  gtk+-3.0 >= $GTK3_REQUIRED_VERSION
                                       gio-2.0 >= $GIO_REQUIRED_VERSION
                                       indicator3 >= $INDICATOR_REQUIRED_VERSION
                                       json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION
                                       dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
                                       dbusmenu-gtk3-0.4 >= $DBUSMENUGTK_REQUIRED_VERSION)
         AC_SUBST(INDICATOR_CFLAGS)
         AC_SUBST(INDICATOR_LIBS)
         AC_DEFINE(HAVE_GTK3, 1, [whether gtk3 is available])
        ],
      [test "x$with_gtk" = x2],
        [PKG_CHECK_MODULES(INDICATOR,  gtk+-2.0 >= $GTK_REQUIRED_VERSION
                                       gio-2.0 >= $GIO_REQUIRED_VERSION
                                       indicator >= $INDICATOR_REQUIRED_VERSION 
                                       json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION
                                       dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
                                       dbusmenu-gtk-0.4 >= $DBUSMENUGTK_REQUIRED_VERSION)
         AC_SUBST(INDICATOR_CFLAGS)
         AC_SUBST(INDICATOR_LIBS)
        ],
      [AC_MSG_FAILURE([Value for --with-gtk was neither 2 nor 3])]
)
AM_CONDITIONAL(USE_GTK3, [test "x$with_gtk" = x3])

LIBAPPINDICATOR_REQUIRED_VERSION=0.2.9

PKG_CHECK_MODULES(TEST, appindicator-0.1 >= $LIBAPPINDICATOR_REQUIRED_VERSION)

AC_SUBST(TEST_CFLAGS)
AC_SUBST(TEST_LIBS)

APP_INDICATOR_INCLUDE_BASE=`$PKG_CONFIG --variable=includedir appindicator-0.1`
APP_INDICATOR_INCLUDE="$APP_INDICATOR_INCLUDE_BASE/libappindicator-0.1/"

AC_SUBST(APP_INDICATOR_INCLUDE)

###########################
# Check to see if we're local
###########################

with_localinstall="no"
AC_ARG_ENABLE(localinstall, AS_HELP_STRING([--enable-localinstall], [install all of the files localy instead of system directories (for distcheck)]), with_localinstall=$enableval, with_localinstall=no)

###########################
# Indicator Info
###########################

if test "x$with_localinstall" = "xyes"; then
	INDICATORDIR="${libdir}/indicators/2/"
	INDICATORICONSDIR="${datadir}/libindicate/icons/"
else
	INDICATORDIR=`$PKG_CONFIG --variable=indicatordir indicator`
	INDICATORICONSDIR=`$PKG_CONFIG --variable=iconsdir indicator`
fi
AC_SUBST(INDICATORDIR)
AC_SUBST(INDICATORICONSDIR)

###########################
# DBus Service Info
###########################

if test "x$with_localinstall" = "xyes"; then
	DBUSSERVICEDIR="${datadir}/dbus-1/services/"
else
	DBUSSERVICEDIR=`$PKG_CONFIG --variable=session_bus_services_dir dbus-1`
fi
AC_SUBST(DBUSSERVICEDIR)

###########################
# Files
###########################

AC_OUTPUT([
Makefile
src/Makefile
data/Makefile
tests/Makefile
])

###########################
# Results
###########################

AC_MSG_NOTICE([

Application Indicator Configuration:

	Prefix:        $prefix
	Indicator Dir: $INDICATORDIR
	GTK+ Version:  $with_gtk
])
