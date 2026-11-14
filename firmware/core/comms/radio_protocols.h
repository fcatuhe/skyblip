#ifndef SKYBLIP_CORE_COMMS_RADIO_PROTOCOLS_H
#define SKYBLIP_CORE_COMMS_RADIO_PROTOCOLS_H

#include <cstdint>

namespace skyblip::comms {

enum class RadioProtocol : uint8_t { Adsb, Adsl, Alptas };

}

#endif
