#!/bin/sh

PKG_NAME="indicator-application"

which mate-autogen || {
	echo "You need mate-common from https://git.mate-desktop.org/mate-common"
	exit 1
}

mate-autogen $@
