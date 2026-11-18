// Harness, not a test: the DF17 frames a transmitter sends, heard through the real decoder.
#ifndef SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H
#define SKYBLIP_TEST_SUPPORT_ADSB_FRAMES_H

#include <cstdint>

#include "core/util/result.h"
#include "protocols/adsb/message.h"
#include "simulator/world/adsb_out.h"

namespace skyblip {

inline Status adsb_hear(adsb::Squitter frame, adsb::Message& message) {
    adsb::seal(frame.bytes);
    return status_of(
        adsb::receive(frame.bytes, adsb::kFrameBytes, adsb::Reception{0, -70}, message));
}

}

#endif
