#ifndef SKYBLIP_CORE_SETTINGS_ADDRESS_H
#define SKYBLIP_CORE_SETTINGS_ADDRESS_H

#include <cstdint>
#include <optional>

namespace skyblip::settings {

constexpr uint32_t kAddressMask = 0x00FFFFFFu;

// INFO: fc 09oct26 skyBlip's ADS-L registry assignment, ticket #228794: a copy applies for its own
constexpr uint8_t kSkyblipAddressMappingTable = 58;
constexpr uint8_t kSkyblipManufacturerPrefix = 0x00;

using AirAddress = std::optional<uint32_t>;

constexpr uint32_t kSerialMask = 0x0000FFFFu;

constexpr uint32_t address_of(uint16_t serial) {
    return (static_cast<uint32_t>(kSkyblipManufacturerPrefix) << 16) | serial;
}

}  // namespace skyblip::settings

#endif
