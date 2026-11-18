#include "products/skyblip_bravo/transponders.h"

namespace skyblip::bravo {

bool Transponders::hear(const adsb::Message& message, uint32_t now_ms) {
    Transponder* transponder = find(message.address);
    if (transponder == nullptr) {
        if (!message.address_verified) {
            unmatched_++;
            return false;
        }
        transponder = admit(message.address, now_ms);
    }
    transponder->heard_ms = now_ms;
    if (message.kind != adsb::Kind::AllCall && message.kind != adsb::Kind::Reply)
        transponder->extended_squitter = true;
    if (message.kind == adsb::Kind::Reply && message.reply.pressure_altitude_valid) {
        transponder->altitude_ms = now_ms;
        transponder->altitude_heard = true;
    }
    return true;
}

// INFO: fc 18nov26 a silent transponder is freed, so its age never wraps round to live again
void Transponders::age_out(uint32_t now_ms) {
    for (Transponder& transponder : transponders_)
        if (transponder.used && now_ms - transponder.heard_ms > kTransponderForgetMs)
            transponder = Transponder{};
}

int Transponders::count() const {
    int heard = 0;
    for (const Transponder& transponder : transponders_)
        if (transponder.used) heard++;
    return heard;
}

int Transponders::mode_s_only() const {
    int heard = 0;
    for (const Transponder& transponder : transponders_)
        if (transponder.used && !transponder.extended_squitter) heard++;
    return heard;
}

int Transponders::mode_s_only_with_altitude(uint32_t now_ms) const {
    int heard = 0;
    for (const Transponder& transponder : transponders_)
        if (transponder.used && !transponder.extended_squitter && transponder.altitude_heard &&
            now_ms - transponder.altitude_ms <= kAltitudeMaxAgeMs)
            heard++;
    return heard;
}

Transponders::Transponder* Transponders::find(uint32_t address) {
    for (Transponder& transponder : transponders_)
        if (transponder.used && transponder.address == address) return &transponder;
    return nullptr;
}

// INFO: fc 18nov26 a full roster gives up the transponder heard longest ago
Transponders::Transponder* Transponders::admit(uint32_t address, uint32_t now_ms) {
    Transponder* chosen = transponders_.data();
    for (Transponder& transponder : transponders_) {
        if (!transponder.used) {
            chosen = &transponder;
            break;
        }
        if (now_ms - transponder.heard_ms > now_ms - chosen->heard_ms) chosen = &transponder;
    }
    *chosen = Transponder{};
    chosen->used = true;
    chosen->address = address;
    return chosen;
}

}  // namespace skyblip::bravo
