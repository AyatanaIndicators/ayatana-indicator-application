cmake_minimum_required(VERSION 3.13)
project(ayatana-indicator-application C)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

# Options

option(ENABLE_TESTS "Enable all tests and checks" OFF)
option(ENABLE_COVERAGE "Enable coverage reports (includes enabling all tests and checks)" OFF)
option(ENABLE_WERROR "Treat all build warnings as errors" OFF)

if(ENABLE_COVERAGE)
    set(ENABLE_TESTS ON)
    set(CMAKE_BUILD_TYPE "Coverage")
else()
    set(CMAKE_BUILD_TYPE "Release")
endif()

if(ENABLE_WERROR)
    add_definitions("-Werror")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_definitions("-Weverything")
else()
    add_definitions("-Wall")
endif()

# Check for prerequisites

set(CMAKE_BUILD_TYPE "Release")
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROJECT_DEPS REQUIRED glib-2.0>=2.58 ayatana-indicator3-0.4>=0.6.2 gtk+-3.0>=3.24 ayatana-appindicator3-0.1>=0.5.3 dbus-glib-1>=0.110)

# Set global variables

include(GNUInstallDirs)
set(PROJECT_VERSION "22.2.0")
set(PROJECT_NAME "ayatana-indicator-application")

# Make everything

add_subdirectory(data)
add_subdirectory(src)

if (ENABLE_TESTS)
    include(CTest)
    enable_testing()
endif()

# Display config info

message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Unit tests: ${ENABLE_TESTS}")
message(STATUS "Build with -Werror: ${ENABLE_WERROR}")
