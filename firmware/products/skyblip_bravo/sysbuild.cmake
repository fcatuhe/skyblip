# MCUboot's shared Kconfig, after products/skyblip_bravo/sysbuild/mcuboot.conf or the list a -D passes.
sysbuild_cache_set(VAR mcuboot_EXTRA_CONF_FILE APPEND REMOVE_DUPLICATES
  ${CMAKE_CURRENT_LIST_DIR}/../common/sysbuild/mcuboot.conf)
