cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../esp32mac/components/macintosh"
    "${CMAKE_CURRENT_LIST_DIR}/../../esp32emu/components/musashi"
    "${CMAKE_CURRENT_LIST_DIR}/../../esp32emu/components/emudbg"
    "${CMAKE_CURRENT_LIST_DIR}/../../esp32emu/components/diskspace")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(papers3_mac)
