#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OWN_TRANSPONDER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OWN_TRANSPONDER_H

#include <array>
#include <cstdint>
#include <optional>

#include "core/comms/peer.h"
#include "core/protocol/adsb.h"

namespace skyblip::bravo {

// INFO: fc 13nov26 the loudest transponder by a clear lead, which the pilot confirms on the Go
class OwnTransponder {
   public:
    // TODO: fc 13nov26 unverified until a cockpit capture measures our transponder and a neighbour
    static constexpr int8_t kLoudFloorDbm = -30;
    static constexpr int kLeadDb = 10;
    static constexpr uint32_t kLeadHoldMs = 10000;
    // INFO: fc 13nov26 a transponder squitters about six times a second, so 5 s quiet is gone
    static constexpr uint32_t kEmitterMaxAgeMs = 5000;
    static constexpr int kTrackedEmitters = 8;

    void hear(const protocol::AdsbMessage& message, uint32_t now_ms);
    void tick(uint32_t now_ms);

    std::optional<comms::OwnTransponderHeard> heard() const;

   private:
    struct Emitter {
        bool used{false};
        uint32_t address{0};
        int32_t level_sixteenths_dbm{0};
        uint32_t heard_ms{0};
        char callsign[comms::kPeerCallsignBytes]{};
        uint8_t category{0};
    };

    Emitter* emitter_for(uint32_t address, uint32_t now_ms);
    static bool live(const Emitter& emitter, uint32_t now_ms);
    const Emitter* leader(uint32_t now_ms) const;

    std::array<Emitter, kTrackedEmitters> emitters_{};
    uint32_t leader_address_{0};
    uint32_t leading_since_ms_{0};
    bool leading_{false};
    bool held_{false};
};

}  // namespace skyblip::bravo

#endif
