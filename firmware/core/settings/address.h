#ifndef SKYBLIP_CORE_SETTINGS_ADDRESS_H
#define SKYBLIP_CORE_SETTINGS_ADDRESS_H

#include <cstdint>

namespace skyblip::settings {

constexpr uint32_t kAddressMask = 0x00FFFFFFu;

constexpr uint32_t kUnusableLow = 0x000000u;
constexpr uint32_t kUnusableHigh = kAddressMask;
constexpr uint32_t kFallbackAddress = 0x5BCAFEu;

// TODO: fc 09oct26 assigned page 49 prefix 0x00: address becomes 0x00 plus a dense 16-bit serial, blocked on issuing serials per unit
constexpr uint8_t kAddrTableSkyblip = 58;

uint32_t air_address(uint32_t addr);

}  // namespace skyblip::settings

#endif
