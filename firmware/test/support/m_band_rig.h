// Harness, not a test: an M-band burst handed to a product rig as the chip delivers it, and the
// tape read back.
#ifndef SKYBLIP_TEST_SUPPORT_M_BAND_RIG_H
#define SKYBLIP_TEST_SUPPORT_M_BAND_RIG_H

#include <cstddef>
#include <cstdint>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "hardware/parts/sx1262/model.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/common/manchester.h"
#include "test/support/product_rig.h"

namespace skyblip {

// A target two metres away, as the chip hands one over: past the shared sync window.
inline void hear_m_band(Rig& rig, uint32_t sync_word, const uint8_t* frame, uint8_t frame_bytes,
                        int dead_chip_byte = -1) {
    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_bytes = wire::manchester_frame(sync_word, frame, frame_bytes, chips);
    if (dead_chip_byte >= 0) chips[dead_chip_byte] ^= 0x01;

    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.band = model::Band::M;
    event.rssi_dbm = -15;
    event.rssi_valid = true;
    event.bytes = models::Sx1262::deliver_after_sync(
        chips, static_cast<uint16_t>(chip_bytes), protocol::kSharedSync, protocol::kSharedSyncBits,
        event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

inline void hear_adsl(Rig& rig, adsl::Packet& packet) {
    packet.scramble();
    packet.set_crc();
    hear_m_band(rig, adsl::kSyncWord, packet.data, adsl::kFrameBytes);
}

inline const radio::Entry* heard_entry(Rig& rig) {
    const radio::Log& log = rig.state().radio_log;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.verdict != radio::Verdict::Transmitted && entry.verdict != radio::Verdict::Lost &&
            entry.verdict != radio::Verdict::Held && entry.verdict != radio::Verdict::Unarmed)
            return &entry;
    }
    return nullptr;
}

// Own-ship's own bursts are the other half of the tape, and a case about receptions ignores them.
inline radio::Verdict heard_verdict(Rig& rig) {
    const radio::Log& log = rig.state().radio_log;
    for (int i = 0; i < log.count(); i++) {
        const radio::Verdict verdict = log.newest(i).verdict;
        if (verdict != radio::Verdict::Transmitted && verdict != radio::Verdict::Lost &&
            verdict != radio::Verdict::Held && verdict != radio::Verdict::Unarmed)
            return verdict;
    }
    return radio::Verdict::Transmitted;
}

inline void fly(Rig& rig, uint32_t& now_ms, uint32_t seconds) {
    rig.seconds(now_ms, seconds, 25000, 900);
}

inline void settle(Rig& rig, uint32_t& now_ms) {
    rig.run(now_ms, now_ms + 200);
    now_ms += 250;
}

}  // namespace skyblip

#endif
