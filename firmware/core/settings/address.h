#ifndef SKYBLIP_CORE_SETTINGS_ADDRESS_H
#define SKYBLIP_CORE_SETTINGS_ADDRESS_H

#include <cstdint>
#include <optional>

#include "protocols/adsl/packet.h"

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

// INFO: fc 14nov26 ICAO Annex 10 Vol III, Appendix to Chapter 9, 5.2 f): never 24 zeros or 24 ones
constexpr bool icao_assignable(uint32_t address) { return address != 0 && address < kAddressMask; }

struct OnAir {
    AirAddress address;
    uint8_t address_mapping_table;
};

// INFO: fc 14nov26 ADS-L F.2.3: a set own ICAO is our address on every payload, once activated
constexpr OnAir on_air(AirAddress unit, uint32_t own_icao) {
    return unit && icao_assignable(own_icao) ? OnAir{own_icao, adsl::kAddressTableIcao}
                                             : OnAir{unit, kSkyblipAddressMappingTable};
}

}  // namespace skyblip::settings

#endif
