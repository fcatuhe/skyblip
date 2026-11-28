#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OWN_TRANSPONDER_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_OWN_TRANSPONDER_H

#include <cstdint>
#include <optional>

#include "core/comms/peer.h"
#include "core/traffic/aboard.h"
#include "protocols/adsb/message.h"

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

    void hear(const adsb::Message& message, uint32_t now_ms);
    void tick(uint32_t now_ms) { aboard_.tick(now_ms); }

    std::optional<comms::OwnTransponderHeard> heard() const;

   private:
    struct Identified {
        char callsign[comms::kPeerCallsignBytes]{};
        uint8_t category{0};
    };

    traffic::Aboard<Identified, kTrackedEmitters> aboard_{
        {kLoudFloorDbm, kLeadDb, kLeadHoldMs, kEmitterMaxAgeMs}};
};

}  // namespace skyblip::bravo

#endif
