cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smp_gate LANGUAGES C CXX)

set(FIRMWARE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_include_directories(app PRIVATE ${FIRMWARE})
target_sources(app PRIVATE
  src/main.cpp
  ${FIRMWARE}/hardware/platform/zephyr/mcumgr_hooks.cpp
  ${FIRMWARE}/core/dfu/smp_policy.cpp
  ${FIRMWARE}/core/dfu/image.cpp
  ${FIRMWARE}/core/dfu/update.cpp
  ${FIRMWARE}/core/util/format.cpp
)
# The upload check reads img_mgmt's request, whose header includes MCUboot's bootutil/image.h.
target_link_libraries(app PRIVATE MCUBOOT_BOOTUTIL)
# bootutil/image.h asserts with C11's _Static_assert, which picolibc maps for C++ and the host glibc does not.
target_compile_definitions(app PRIVATE $<$<COMPILE_LANGUAGE:CXX>:_Static_assert=static_assert>)
