cmake_minimum_required(VERSION 3.20.0)

# skyBlip Bravo ships on a Seeed MeshTracker X1, so the board is declared here and another -b is refused.
set(SKYBLIP_PRODUCT_NAME skyblip_bravo)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/skyblip.cmake)

skyblip_product_board(meshtracker_x1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skyblip_bravo LANGUAGES C CXX)

skyblip_shared_layers()

file(GLOB SKYBLIP_BRAVO_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/services/*.cpp
)
target_sources(app PRIVATE ${SKYBLIP_BRAVO_SOURCES})
