# ayatana-indicator-application.service

pkg_check_modules(SYSTEMD systemd)

if (${SYSTEMD_FOUND})
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ayatana-indicator-application.service.in" "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.service" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.service" DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/systemd/user")
endif()

# ayatana-indicator-application.desktop

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ayatana-indicator-application.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.desktop" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application.desktop" DESTINATION "${CMAKE_INSTALL_PREFIX}/etc/xdg/autostart")

# ordering-override.keyfile

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ordering-override.keyfile" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/ayatana-indicator-application")


