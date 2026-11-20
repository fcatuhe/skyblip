cmake_minimum_required(VERSION 3.20.0)

# skyBlip Go ships on a T-Echo Plus, so the board is declared here and another -b is refused.
set(SKYBLIP_PRODUCT_NAME skyblip_go)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/skyblip.cmake)

skyblip_product_board(t_echo_plus)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skyblip_go LANGUAGES C CXX)

skyblip_shared_layers()

file(GLOB SKYBLIP_GO_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/pages/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/services/*.cpp
)
target_sources(app PRIVATE ${SKYBLIP_GO_SOURCES})
