#include "hardware/platform/zephyr/imu_firmware.h"

#include <zephyr/devicetree.h>

#include "hardware/parts/bhi260/image_store.h"

namespace skyblip::platform::zephyr {

#if defined(CONFIG_SKYBLIP_IMU_IMAGE_LINKED)

namespace {

const uint8_t kLinked[] = {
#include "bhi260ap_fw.inc"
};

}  // namespace

static_assert(sizeof(kLinked) + parts::Bhi260ImageStore::kHeaderBytes <=
                  DT_REG_SIZE(DT_NODELABEL(imu_image_partition)),
              "the BHI260AP image no longer fits the partition a slim image boots it from");

const uint8_t* const kImuFirmware = kLinked;
const size_t kImuFirmwareBytes = sizeof(kLinked);

#else

const uint8_t* const kImuFirmware = nullptr;
const size_t kImuFirmwareBytes = 0;

#endif

const Sha256::Digest kImuFirmwareDigest = {
#include "bhi260ap_fw_sha256.inc"
};

}  // namespace skyblip::platform::zephyr
