cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lr2021_adsb_receiver LANGUAGES C CXX)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_sources(app PRIVATE src/main.cpp ../../../hardware/parts/lr2021/lr2021.cpp)
