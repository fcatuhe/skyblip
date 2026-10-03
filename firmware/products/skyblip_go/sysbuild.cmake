# The two protected TLVs the update page reads: which hub image this build pins, and whether it carries it.
file(SHA256 ${CMAKE_CURRENT_LIST_DIR}/../../hardware/parts/bhi260/firmware/BHI260AP.fw imu_image_digest)
if(SB_CONFIG_SKYBLIP_IMU_IMAGE_LINKED)
  set(imu_image_linked TRUE)
  set(imu_image_carried 01)
else()
  set(imu_image_linked FALSE)
  set(imu_image_carried 00)
endif()
set_config_bool(${DEFAULT_IMAGE} CONFIG_SKYBLIP_IMU_IMAGE_LINKED ${imu_image_linked})
set_config_string(${DEFAULT_IMAGE} CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS
  "--custom-tlv 0xa0 0x${imu_image_digest} --custom-tlv 0xa1 0x${imu_image_carried}")
