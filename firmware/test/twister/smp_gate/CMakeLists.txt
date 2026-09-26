cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smp_gate LANGUAGES C CXX)

set(FIRMWARE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_include_directories(app PRIVATE ${FIRMWARE})
target_sources(app PRIVATE
  src/main.cpp
  ${FIRMWARE}/hardware/platform/zephyr/mcumgr_hooks.cpp
  ${FIRMWARE}/core/dfu/smp_policy.cpp
)
