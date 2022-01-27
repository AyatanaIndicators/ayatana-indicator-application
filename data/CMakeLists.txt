# ayatana-indicator-application.service

###########################
# Systemd Job
###########################

pkg_check_modules(SYSTEMD systemd)
if (${SYSTEMD_FOUND})

  # where to install
  pkg_get_variable(SYSTEMD_USER_DIR systemd systemduserunitdir)
  message (STATUS "${SYSTEMD_USER_DIR} is the systemd user unit file install dir")

  set (SYSTEMD_USER_NAME "${CMAKE_PROJECT_NAME}.service")
  set (SYSTEMD_USER_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SYSTEMD_USER_NAME}")
  set (SYSTEMD_USER_FILE_IN "${CMAKE_CURRENT_SOURCE_DIR}/${SYSTEMD_USER_NAME}.in")

  # build it
  set (pkglibexecdir "${CMAKE_INSTALL_FULL_PKGLIBEXECDIR}")
  configure_file ("${SYSTEMD_USER_FILE_IN}" "${SYSTEMD_USER_FILE}")

  # install it
  install (FILES "${SYSTEMD_USER_FILE}"
           DESTINATION "${SYSTEMD_USER_DIR}")
endif()

###########################
# XDG Autostart
###########################

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ayatana-indicator-application.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.desktop" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.desktop" DESTINATION "/etc/xdg/autostart")

###########################
# ordering-override.keyfile
###########################

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ordering-override.keyfile" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/ayatana-indicator-application")
