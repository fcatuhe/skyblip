#ifndef SKYBLIP_CORE_SETTINGS_ADDRESS_H
#define SKYBLIP_CORE_SETTINGS_ADDRESS_H

#include <cstdint>
#include <optional>

namespace skyblip::settings {

constexpr uint32_t kAddressMask = 0x00FFFFFFu;

// INFO: fc 09oct26 registry ticket #228794: manufacturer page 49, which F.2.2 numbers from 9
constexpr uint8_t kAddressMappingTable = 58;
constexpr uint8_t kManufacturerPrefix = 0x00;

using AirAddress = std::optional<uint32_t>;

constexpr uint32_t address_of(uint16_t serial) {
    return (static_cast<uint32_t>(kManufacturerPrefix) << 16) | serial;
}

}  // namespace skyblip::settings

#endif
