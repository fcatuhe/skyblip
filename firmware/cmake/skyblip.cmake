# Shared by every product's Zephyr build: the include root and the layers that
# go into an image. A product's own CMakeLists declares its board and adds
# itself; nothing here knows which product is being built.

set(SKYBLIP_FIRMWARE ${CMAKE_CURRENT_LIST_DIR}/..)

# Globbed per layer, exactly like the host build: a source that exists is built.
# simulator/ and the part models are host-only and never enter an image.
function(skyblip_shared_layers)
  target_include_directories(app PRIVATE ${SKYBLIP_FIRMWARE} ${SKYBLIP_FIRMWARE}/vendor)

  file(GLOB_RECURSE shared
    ${SKYBLIP_FIRMWARE}/core/*.cpp
    ${SKYBLIP_FIRMWARE}/ui/*.cpp
    ${SKYBLIP_FIRMWARE}/runtime/*.cpp
    ${SKYBLIP_FIRMWARE}/hardware/parts/*.cpp
    ${SKYBLIP_FIRMWARE}/hardware/platform/zephyr/*.cpp
  )
  list(FILTER shared EXCLUDE REGEX "/test_[^/]*\\.cpp$")
  target_sources(app PRIVATE ${shared})
  # mcumgr_hooks.cpp reads an upload request, whose header includes MCUboot's bootutil/image.h.
  if(CONFIG_MCUMGR_GRP_IMG)
    target_link_libraries(app PRIVATE MCUBOOT_BOOTUTIL)
  endif()
  skyblip_imu_image()
endfunction()

# The hub boots from our copy of its image: every build pins the digest, only a full one carries it.
function(skyblip_imu_image)
  set(image ${SKYBLIP_FIRMWARE}/hardware/parts/bhi260/firmware/BHI260AP.fw)
  set(generated ${ZEPHYR_BINARY_DIR}/include/generated)
  if(CONFIG_SKYBLIP_IMU_IMAGE_LINKED)
    generate_inc_file_for_target(app ${image} ${generated}/bhi260ap_fw.inc)
  endif()
  file(SHA256 ${image} digest)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " digest_bytes ${digest})
  file(GENERATE OUTPUT ${generated}/bhi260ap_fw_sha256.inc CONTENT "${digest_bytes}\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${image})
endfunction()

# One product, one board. Called before find_package(Zephyr) so a mismatched
# pair is refused at configure time rather than diagnosed by the compiler.
function(skyblip_product_board board)
  # Compared bare: the first configure resolves t_echo_plus to
  # t_echo_plus/nrf52840 and caches that, and the qualified name is what a
  # reconfigure of an existing build directory hands back.
  string(REGEX REPLACE "/.*$" "" requested "${BOARD}")
  if(DEFINED BOARD AND NOT "${requested}" STREQUAL "${board}")
    message(FATAL_ERROR
      "${PROJECT_NAME}${SKYBLIP_PRODUCT_NAME} is a ${board} product: refusing to build it for ${BOARD}")
  endif()
  if(NOT DEFINED BOARD)
    set(BOARD ${board} CACHE STRING "board this product ships on" FORCE)
  endif()
endfunction()
