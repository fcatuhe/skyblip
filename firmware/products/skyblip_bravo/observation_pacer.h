#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OBSERVATION_PACER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OBSERVATION_PACER_H

#include <array>
#include <cstdint>

#include "core/comms/link_sessions.h"
#include "core/traffic/table.h"

namespace skyblip::bravo {

// INFO: fc 11nov26 each Go is owed a target once the table took news of it, and owed it alone
class ObservationPacer {
   public:
    // INFO: fc 11nov26 schemas/link.v1.json observation: two frames a second for a target at most
    static constexpr uint32_t kTargetUpdateFloorMs = 500;
    static constexpr int kRecipients = static_cast<int>(comms::LinkSessions::kMaxSessions);

    bool owed(int recipient, int slot, const traffic::Target& target, uint32_t callsign_heard_s,
              uint32_t now_ms) const {
        const Sent& sent = sent_[static_cast<size_t>(recipient)][static_cast<size_t>(slot)];
        if (!sent.used || sent.address != target.observation.address) return true;
        if (sent.revision == target.revision && sent.callsign_heard_s == callsign_heard_s)
            return false;
        return now_ms - sent.at_ms >= kTargetUpdateFloorMs;
    }

    void spend(int recipient, int slot, const traffic::Target& target, uint32_t callsign_heard_s,
               uint32_t now_ms) {
        sent_[static_cast<size_t>(recipient)][static_cast<size_t>(slot)] =
            Sent{target.observation.address, target.revision, callsign_heard_s, now_ms, true};
    }

    // INFO: fc 11nov26 a Go new to a seat is owed the whole table, whatever the last one was sent
    void forget(int recipient) { sent_[static_cast<size_t>(recipient)] = {}; }

   private:
    struct Sent {
        uint32_t address{0};
        uint32_t revision{0};
        uint32_t callsign_heard_s{0};
        uint32_t at_ms{0};
        bool used{false};
    };

    std::array<std::array<Sent, traffic::TrafficTable::kMaxTargets>, kRecipients> sent_{};
};

}  // namespace skyblip::bravo

#endif
