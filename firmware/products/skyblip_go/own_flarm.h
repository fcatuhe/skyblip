#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_OWN_FLARM_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_OWN_FLARM_H

#include <cstdint>
#include <optional>

#include "core/model/aircraft.h"
#include "core/settings/address.h"
#include "core/traffic/aboard.h"

namespace skyblip::go {

// INFO: fc 27nov26 the loudest 868 MHz emitter by a clear lead, which the pilot confirms as aboard
class OwnFlarm {
   public:
    // TODO: fc 27nov26 unverified until a cockpit capture measures our FLARM and a neighbour's
    static constexpr int8_t kLoudFloorDbm = -50;
    static constexpr int kLeadDb = 10;
    // INFO: fc 27nov26 three ADS-L ground positions, which come one every 9 to 11 s
    static constexpr uint32_t kLeadHoldMs = 30000;
    // INFO: fc 27nov26 an ADS-L ground position comes every 9 to 11 s, so 25 s quiet is two missed
    static constexpr uint32_t kEmitterMaxAgeMs = 25000;
    static constexpr int kTrackedEmitters = 8;

    // INFO: fc 27nov26 every direct emitter, ours under an ICAO address included, so none hides
    void hear(const model::AircraftObservation& observation, uint32_t now_ms) {
        const uint32_t key = (static_cast<uint32_t>(observation.address_mapping_table) << 24) |
                             (observation.address & settings::kAddressMask);
        (void)aboard_.hear(key, observation.rssi_valid, observation.rssi_dbm, now_ms);
    }

    void tick(uint32_t now_ms) { aboard_.tick(now_ms); }

    std::optional<traffic::AboardHeard> heard() const {
        const auto* emitter = aboard_.held();
        if (emitter == nullptr) return std::nullopt;
        return traffic::AboardHeard{emitter->key & settings::kAddressMask,
                                    static_cast<uint8_t>(emitter->key >> 24)};
    }

   private:
    struct Nothing {};

    traffic::Aboard<Nothing, kTrackedEmitters> aboard_{
        {kLoudFloorDbm, kLeadDb, kLeadHoldMs, kEmitterMaxAgeMs}};
};

}  // namespace skyblip::go

#endif
