#include "products/skyblip_bravo/own_transponder.h"

#include <cstring>

#include "core/protocol/adsb_category.h"

namespace skyblip::bravo {

void OwnTransponder::hear(const adsb::Message& message, uint32_t now_ms) {
    // INFO: fc 13nov26 a misconfigured transponder squitters 000000, which as aircraft address
    // means unset
    if (message.address == 0) return;
    Identified* identified = aboard_.hear(message.address, message.reception.rssi_valid,
                                          message.reception.rssi_dbm, now_ms);
    if (identified == nullptr || message.kind != adsb::Kind::Identification) return;
    if (message.identification.callsign_valid)
        std::strncpy(identified->callsign, message.identification.callsign,
                     sizeof(identified->callsign) - 1);
    identified->category =
        protocol::adsb_emitter_to_own_category(message.type_code, message.identification.category);
}

std::optional<comms::OwnTransponderHeard> OwnTransponder::heard() const {
    const auto* emitter = aboard_.held();
    if (emitter == nullptr) return std::nullopt;
    comms::OwnTransponderHeard out{};
    out.address = emitter->key;
    std::memcpy(out.callsign, emitter->detail.callsign, sizeof(out.callsign));
    out.aircraft_category = emitter->detail.category;
    return out;
}

}  // namespace skyblip::bravo
