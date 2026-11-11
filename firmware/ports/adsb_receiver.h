#ifndef SKYBLIP_PORTS_ADSB_RECEIVER_H
#define SKYBLIP_PORTS_ADSB_RECEIVER_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::ports {

struct AdsbReceiverStatistics {
    static constexpr int kRssiBuckets = 8;
    // INFO: fc 11nov26 the floor under each bucket in dBm: the detection threshold, then 10 dB
    // steps
    static constexpr int8_t kRssiFloorsDbm[kRssiBuckets] = {-128, -90, -80, -70,
                                                            -60,  -50, -40, -30};
    static constexpr uint64_t kSecondUs = 1000000;

    uint32_t frames{0};
    uint32_t frame_rate_hz{0};
    uint32_t dropped{0};
    uint32_t rearms{0};
    uint32_t resets{0};
    uint32_t command_errors{0};
    uint16_t device_errors{0};
    uint32_t rssi[kRssiBuckets]{};
    uint64_t counting_second{0};
    uint32_t counted{0};

    void heard(int8_t rssi_dbm, bool rssi_valid, uint64_t at_us) {
        roll(at_us);
        frames++;
        counted++;
        if (rssi_valid) rssi[bucket_of(rssi_dbm)]++;
    }

    // INFO: fc 11nov26 frame_rate_hz is the last whole second, so a silent second reads zero
    void roll(uint64_t now_us) {
        const uint64_t second = now_us / kSecondUs;
        if (second == counting_second) return;
        frame_rate_hz = second == counting_second + 1 ? counted : 0;
        counting_second = second;
        counted = 0;
    }

    static int bucket_of(int8_t rssi_dbm) {
        int bucket = 0;
        while (bucket + 1 < kRssiBuckets && rssi_dbm >= kRssiFloorsDbm[bucket + 1]) bucket++;
        return bucket;
    }
};

class AdsbReceiver {
   public:
    virtual ~AdsbReceiver() = default;

    virtual Status begin() = 0;
    virtual AdsbReceiverStatistics statistics() const = 0;
    // INFO: fc 11nov26 terminal like ports::Rf::sleep(): parked() turns true only once the part
    // confirmed its sleep
    virtual void sleep() = 0;
    virtual bool parked() const = 0;
    // INFO: fc 11nov26 the shutdown's fallback past its ceiling: the part is held in reset, which
    // receives nothing
    virtual void abandon() = 0;
};

}  // namespace skyblip::ports

#endif
