#ifndef SKYBLIP_CORE_EVENTS_RF_H
#define SKYBLIP_CORE_EVENTS_RF_H

#include <array>
#include <cstdint>

#include "core/model/band.h"

namespace skyblip::events {
// INFO: fc 14nov26 Busy: the assessment before a burst found the channel taken, and nothing keyed
enum class RfEventType : uint8_t { RxDone, CrcError, TxDone, Missed, Busy };

// INFO: fc 05aug26 fits the uplink's RS(255,223) codeword (§C.4): truncation reads as damage
constexpr int kRfEventBytes = 255;

struct RfEvent {
    RfEventType type;
    // INFO: fc 20nov26 M by default, so an unstamped entry is framed rather than trusted
    model::Band band;
    uint8_t bytes;
    int8_t rssi_dbm;
    bool rssi_valid;
    uint64_t at_us;
    uint64_t keyed_at_us;
    uint64_t staged_at_us;
    // INFO: fc 27sep26 the instant the burst was armed for, which names it: a dwell can carry two
    uint64_t tx_at_us;
    uint32_t frequency_hz;
    std::array<uint8_t, kRfEventBytes> data;
};

}  // namespace skyblip::events

#endif
