#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_GATE_REFERENCE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_GATE_REFERENCE_H

#include <cstdint>

#include "core/comms/peer.h"
#include "core/diagnostics/record.h"
#include "core/model/ownship.h"

namespace skyblip::bravo {

struct GateChoice {
    comms::ExportReference reference{comms::ExportReference::None};
    model::Ownship own{};
};

// INFO: fc 11nov26 where Bravo places traffic: its Go's from a fresh claim, else its own fix
class GateReference {
   public:
    // INFO: fc 11nov26 a fix period, a second of unknown phase and two claim periods, one lost
    static constexpr uint32_t kClaimMaxAgeMs = 4000;

    void hear(const comms::PeerClaim& claim, const diagnostics::Instant& at, uint32_t now_ms);
    void lose_link() { heard_ = false; }
    void unpair();

    GateChoice choose(const model::Ownship& own, uint32_t now_ms) const;
    uint32_t own_icao(uint32_t bravo_own_icao) const;

   private:
    bool fresh(uint32_t age_ms, uint32_t now_ms) const;

    comms::PeerClaim claim_{};
    uint32_t heard_ms_{0};
    uint32_t transit_ms_{0};
    uint32_t go_own_icao_{0};
    bool heard_{false};
};

}  // namespace skyblip::bravo

#endif
