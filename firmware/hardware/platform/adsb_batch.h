#ifndef SKYBLIP_HARDWARE_PLATFORM_ADSB_BATCH_H
#define SKYBLIP_HARDWARE_PLATFORM_ADSB_BATCH_H

#include <cstdint>
#include <cstring>

#include "core/bus/bus.h"
#include "core/events/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "ports/adsb_receiver.h"

namespace skyblip::platform {

static_assert(parts::lr2021::kFifoFrameBytes == adsb::kFrameBytes);

constexpr size_t kAdsbBatchBytes =
    static_cast<size_t>(parts::lr2021::kFifoFrames) * parts::lr2021::kFifoFrameBytes;

// INFO: fc 27nov26 a batch is dated when the thread woke, and only its newest frame has an RSSI
inline void hand_over(const uint8_t* batch, const parts::Lr2021Event& event, uint64_t woke_us,
                      ports::AdsbReceiverStatistics& statistics, bus::AdsbQueue& out) {
    for (uint8_t i = 0; i < event.frames; i++) {
        const bool newest = i + 1 == event.frames;
        events::AdsbFrame frame{};
        std::memcpy(frame.bytes.data(), batch + static_cast<size_t>(i) * adsb::kFrameBytes,
                    adsb::kFrameBytes);
        frame.at_us = woke_us;
        frame.rssi_valid = newest && event.rssi_valid;
        frame.rssi_dbm = frame.rssi_valid ? event.rssi_dbm : 0;
        if (statistics.admit(frame.bytes.data(), frame.rssi_dbm, frame.rssi_valid, woke_us))
            (void)out.push(frame);
    }
}

}  // namespace skyblip::platform

#endif
