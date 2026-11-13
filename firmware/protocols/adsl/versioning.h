#ifndef SKYBLIP_PROTOCOLS_ADSL_VERSIONING_H
#define SKYBLIP_PROTOCOLS_ADSL_VERSIONING_H

#include <cstdint>

#include "protocols/adsl/packet.h"

namespace skyblip::adsl {

constexpr int32_t kCloseHorizontalM = 1000;
constexpr int32_t kCloseVerticalM = 800;
constexpr int32_t kCloseTcpaS = 30;

// INFO: fc 13nov26 B.5: a Status names its sender's highest version, and any other packet stands in
uint8_t declared_max_version(const Packet& clear);

// INFO: fc 13nov26 B.5: within 1 km and 800 m, or under 800 m and closing to CPA in less than 30 s
bool close_client(int32_t horizontal_m, int32_t vertical_m, int32_t closing_mm_s);

}

#endif
