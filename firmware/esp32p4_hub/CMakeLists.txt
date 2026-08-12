cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/aquacyd_link"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/aquahub_core"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.1.0")
project(aquahub_esp32p4)
