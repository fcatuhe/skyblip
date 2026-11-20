#ifndef SKYBLIP_PRODUCTS_COMMON_TRAFFIC_SETTINGS_H
#define SKYBLIP_PRODUCTS_COMMON_TRAFFIC_SETTINGS_H

#include <cstdint>

namespace skyblip::common {

struct TrafficSettings {
    TrafficSettings(const uint32_t& aircraft_address_of) : aircraft_address(aircraft_address_of) {}
    TrafficSettings(uint32_t&&) = delete;
    TrafficSettings(const uint32_t&&) = delete;

    const uint32_t& aircraft_address;
};

}  // namespace skyblip::common

#endif
