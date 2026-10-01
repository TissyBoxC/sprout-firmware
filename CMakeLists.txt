cmake_minimum_required(VERSION 3.16.0)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" SPROUT_FIRMWARE_VERSION
     LIMIT_COUNT 1)
string(STRIP "${SPROUT_FIRMWARE_VERSION}" SPROUT_FIRMWARE_VERSION)
if(NOT SPROUT_FIRMWARE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version.")
endif()
set(PROJECT_VER "${SPROUT_FIRMWARE_VERSION}")

list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sprout_firmware)
