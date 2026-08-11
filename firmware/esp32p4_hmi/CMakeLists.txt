cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/aquacyd_link"
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/aquahub_core"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aquahub_esp32p4)
