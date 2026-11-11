#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SELF_TEST_PARTS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SELF_TEST_PARTS_H

#include "ports/capabilities.h"
#include "products/skyblip_go/pages/self_test.h"

namespace skyblip::go {

struct SelfTestPartSpec {
    const char* name;
    ports::Capability capability;
    const char* wired_part;
};

constexpr SelfTestPartSpec kSelfTestParts[] = {
    {"RADIO", ports::Capability::Rf, "SX1262"},
    {"GNSS", ports::Capability::Gnss, "L76K"},
    {"BARO", ports::Capability::Barometer, "BME280"},
    {"PANEL", ports::Capability::Display, "SSD1681"},
    {"IMU", ports::Capability::Inclinometer, "BHI260AP"},
    {"TEMP", ports::Capability::DieTemperature, "NRF52840"},
    {"BATTERY", ports::Capability::Battery, "DIVIDER"},
    {"CONTACTS", ports::Capability::Contacts, "P1.10 P0.11"},
    {"BUZZER", ports::Capability::Buzzer, "PIEZO"},
    {"HAPTIC", ports::Capability::Haptic, "DRV2605"},
    {"LAMP", ports::Capability::Indicator, "RGB"},
    {"LINK", ports::Capability::Link, "BLE"},
    {"STORAGE", ports::Capability::Storage, "NVS+NOR"},
    {"DFU", ports::Capability::Dfu, "MCUBOOT"},
};

constexpr const char* kStorageOnDefaults = "NVS+NOR DEFAULTS";
constexpr const char* kImuDetailWithoutImage = "BHI260AP NOBLOB";

constexpr int kSelfTestPartCount =
    static_cast<int>(sizeof(kSelfTestParts) / sizeof(kSelfTestParts[0]));
// INFO: fc 11nov26 the bus scan and the own-ICAO warning each take a row under the parts
constexpr int kSelfTestRowsBelowParts = 2;
static_assert(kSelfTestPartCount + kSelfTestRowsBelowParts <= kSelfTestRows,
              "the self-test page would drop the bus scan or the own-ICAO warning");

}  // namespace skyblip::go

#endif
