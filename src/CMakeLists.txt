# gen-ayatana-application-service.xml.h

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen-ayatana-application-service.xml.h" "extern const char * _ayatana_application_service;")

# gen-ayatana-application-service.xml.c

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ayatana-application-service.xml" GEN_AYATANA_APPLICATION_SERVICE_XML_C)
string(REPLACE "\"" "\\\"" GEN_AYATANA_APPLICATION_SERVICE_XML_C ${GEN_AYATANA_APPLICATION_SERVICE_XML_C})
string(REPLACE "\n" "\\n\"\n\"" GEN_AYATANA_APPLICATION_SERVICE_XML_C ${GEN_AYATANA_APPLICATION_SERVICE_XML_C})
string(REGEX REPLACE "\n\"$" "\n" GEN_AYATANA_APPLICATION_SERVICE_XML_C ${GEN_AYATANA_APPLICATION_SERVICE_XML_C})
string(PREPEND GEN_AYATANA_APPLICATION_SERVICE_XML_C "const char * _ayatana_application_service = \n\"")
string(APPEND GEN_AYATANA_APPLICATION_SERVICE_XML_C "\;")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen-ayatana-application-service.xml.c" ${GEN_AYATANA_APPLICATION_SERVICE_XML_C})

# ayatana-application-service-marshal.h

find_program(GLIB_GENMARSHAL glib-genmarshal)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.h"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND
    ${GLIB_GENMARSHAL}
    --prefix=_ayatana_application_service_marshal
    --header ayatana-application-service-marshal.list
    --quiet
    --output="${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.h"
)

# ayatana-application-service-marshal.c

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.c"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.h"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND
    ${GLIB_GENMARSHAL}
    --prefix=_ayatana_application_service_marshal
    --body ayatana-application-service-marshal.list
    --include-header=ayatana-application-service-marshal.h
    --quiet
    --output="${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.c"
)

# ayatana-notification-watcher-server.h

find_program(DBUS_BINDING_TOOL dbus-binding-tool)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ayatana-notification-watcher-server.h"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ayatana-application-service-marshal.c"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND
    ${DBUS_BINDING_TOOL}
    --prefix=_ayatana_notification_watcher_server
    --mode=glib-server
    --output="${CMAKE_CURRENT_BINARY_DIR}/ayatana-notification-watcher-server.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ayatana-notification-watcher.xml"
)

# ayatana-notification-watcher-client.h

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ayatana-notification-watcher-client.h"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ayatana-notification-watcher-server.h"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND
    ${DBUS_BINDING_TOOL}
    --prefix=_ayatana_notification_watcher_client
    --mode=glib-client
    --output="${CMAKE_CURRENT_BINARY_DIR}/ayatana-notification-watcher-client.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/ayatana-notification-watcher.xml"
)

add_custom_target("src-generated" DEPENDS "ayatana-notification-watcher-client.h")

# libayatana-application.so

set(SOURCES
    ayatana-application-service-marshal.c
    indicator-application.c
)

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS ${COMPILE_FLAGS})
add_library("ayatana-application" SHARED ${SOURCES})
target_compile_definitions("ayatana-application" PUBLIC G_LOG_DOMAIN="Ayatana-Indicator-Application")
target_include_directories("ayatana-application" PUBLIC ${PROJECT_DEPS_INCLUDE_DIRS})
target_include_directories("ayatana-application" PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries("ayatana-application" ${PROJECT_DEPS_LIBRARIES})
add_dependencies("ayatana-application" "src-generated")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libayatana-application.so" DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/ayatana-indicators3/7")

# ayatana-indicator-application-service

set(SOURCES
    application-service.c
    application-service-appstore.c
    ayatana-application-service-marshal.c
    application-service-watcher.c
    gen-ayatana-application-service.xml.c
    generate-id.c
)

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS ${COMPILE_FLAGS})
add_executable("ayatana-indicator-application-service" ${SOURCES})
target_compile_definitions("ayatana-indicator-application-service" PUBLIC DATADIR="${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/ayatana-indicator-application")
target_compile_definitions("ayatana-indicator-application-service" PUBLIC G_LOG_DOMAIN="ayatana-indicator-application-service")
target_include_directories("ayatana-indicator-application-service" PUBLIC ${PROJECT_DEPS_INCLUDE_DIRS})
target_include_directories("ayatana-indicator-application-service" PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries("ayatana-indicator-application-service" ${PROJECT_DEPS_LIBRARIES})
add_dependencies("ayatana-indicator-application-service" "ayatana-application")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ayatana-indicator-application-service" DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR}/ayatana-indicator-application")
