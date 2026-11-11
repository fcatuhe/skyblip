cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ag3335_adapter LANGUAGES C CXX)

set(firmware ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_include_directories(app PRIVATE ${firmware})
target_sources(app PRIVATE
  src/main.cpp
  ${firmware}/hardware/parts/ag3335/ag3335.cpp
  ${firmware}/hardware/parts/ag3335/pair.cpp
  ${firmware}/core/gnss/nmea.cpp
  ${firmware}/core/gnss/sky.cpp
  ${firmware}/core/gnss/validity.cpp
)
