cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(platform_adapters LANGUAGES C CXX)

set(FIRMWARE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_include_directories(app PRIVATE ${FIRMWARE})
target_sources(app PRIVATE src/main.cpp src/rf.cpp
  ${FIRMWARE}/hardware/parts/sx1262/sx1262.cpp
  ${FIRMWARE}/core/timing/channel.cpp)
