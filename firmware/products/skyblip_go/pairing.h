#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAIRING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAIRING_H

#include <cstdint>

namespace skyblip::go {

enum class BravoStep : uint8_t { Idle, Scanning, Connecting, Waiting, Refused };

enum class BravoRefusal : uint8_t {
    None,
    PhonesConnected,
    NarrowLink,
    NoAnswer,
    LinkLost,
    UnknownProtocol
};

enum class BravoLink : uint8_t { Down, Up, Narrow, Unsupported };

constexpr int kBravoFoundRows = 4;

}  // namespace skyblip::go

#endif
