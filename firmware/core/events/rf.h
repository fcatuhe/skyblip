#ifndef SKYBLIP_CORE_EVENTS_RF_H
#define SKYBLIP_CORE_EVENTS_RF_H

#include <array>
#include <cstdint>

#include "core/model/band.h"

namespace skyblip::events {
enum class RfEventType : uint8_t { RxDone, CrcError, TxDone, Missed };

// INFO: fc 05aug26 The longest burst any dwell reads is the O-band uplink frame,
// an RS(255,223) codeword (§C.4, core/protocol/adsl_uplink.h), and this event is
// where the executor copies what the radio reported. A buffer shorter than the
// codeword truncates it, and Reed-Solomon then reports damage the air never did.
// core/protocol/adsl_uplink.h static_asserts the frame still fits.
constexpr int kRfEventBytes = 255;

struct RfEvent {
    RfEventType type;
    // Which dwell reported this, which is what names the system it belongs to.
    // M by default so a queue entry nobody stamped reads as the band that
    // carries two systems and is framed rather than trusted.
    model::Band band;
    uint8_t len;
    int8_t rssi_dbm;
    bool rssi_valid;
    uint64_t at_us;
    uint64_t keyed_at_us;
    // INFO: fc 27sep26 the instant the burst was armed for, which names it: a dwell can carry two
    uint64_t tx_at_us;
    uint32_t freq_hz;
    std::array<uint8_t, kRfEventBytes> data;
};

}  // namespace skyblip::events

#endif
