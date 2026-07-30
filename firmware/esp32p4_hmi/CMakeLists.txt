cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../lib/aquacyd_link"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aquacyd_esp32p4_hmi)
