cmake_minimum_required(VERSION 3.20.0)

# Configured, never built: Zephyr's app target and binary directory are stood in for, so this runs without a west workspace.
project(shared_layers LANGUAGES C CXX)
add_library(app STATIC)
set(ZEPHYR_BINARY_DIR ${CMAKE_BINARY_DIR}/zephyr)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/skyblip.cmake)
skyblip_shared_layers()

get_target_property(sources app SOURCES)
string(REPLACE ";" "\n" sources "${sources}")
file(WRITE ${CMAKE_BINARY_DIR}/app_sources.txt "${sources}\n")
