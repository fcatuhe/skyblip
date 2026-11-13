#include "products/skyblip_bravo/own_transponder.h"

#include <cstring>

#include "core/protocol/adsb_category.h"

namespace skyblip::bravo {

namespace {

constexpr int32_t kSixteenths = 16;
// INFO: fc 13nov26 a frame moves the level a quarter of the way, so one fade decides nothing
constexpr int32_t kLevelDampingSamples = 4;

}  // namespace

void OwnTransponder::hear(const adsb::Message& message, uint32_t now_ms) {
    // INFO: fc 13nov26 a misconfigured transponder squitters 000000, which as own ICAO means unset
    if (message.address == 0) return;
    Emitter* emitter = nullptr;
    for (Emitter& candidate : emitters_)
        if (candidate.used && candidate.address == message.address) emitter = &candidate;
    if (message.reception.rssi_valid) {
        const int32_t level = message.reception.rssi_dbm * kSixteenths;
        if (emitter == nullptr) {
            emitter = emitter_for(message.address, now_ms);
            emitter->level_sixteenths_dbm = level;
        } else {
            emitter->level_sixteenths_dbm +=
                (level - emitter->level_sixteenths_dbm) / kLevelDampingSamples;
        }
        emitter->heard_ms = now_ms;
    }
    if (emitter == nullptr || message.kind != adsb::Kind::Identification) return;
    if (message.identification.callsign_valid)
        std::strncpy(emitter->callsign, message.identification.callsign,
                     sizeof(emitter->callsign) - 1);
    emitter->category =
        protocol::adsb_emitter_to_adsl_category(message.type_code, message.identification.category);
}

// INFO: fc 13nov26 a quiet emitter is freed, so its age never wraps round to live after 49.7 days
void OwnTransponder::tick(uint32_t now_ms) {
    for (Emitter& emitter : emitters_)
        if (emitter.used && !live(emitter, now_ms)) emitter = Emitter{};
    const Emitter* loudest = leader(now_ms);
    if (loudest == nullptr) {
        leading_ = false;
        held_ = false;
        return;
    }
    if (!leading_ || loudest->address != leader_address_) {
        leading_ = true;
        leader_address_ = loudest->address;
        leading_since_ms_ = now_ms;
    }
    held_ = now_ms - leading_since_ms_ >= kLeadHoldMs;
}

std::optional<comms::OwnTransponderHeard> OwnTransponder::heard() const {
    if (!held_) return std::nullopt;
    for (const Emitter& emitter : emitters_) {
        if (!emitter.used || emitter.address != leader_address_) continue;
        comms::OwnTransponderHeard out{};
        out.address = emitter.address;
        std::memcpy(out.callsign, emitter.callsign, sizeof(out.callsign));
        out.aircraft_category = emitter.category;
        return out;
    }
    return std::nullopt;
}

// INFO: fc 13nov26 a full table gives up an emitter gone quiet first, then the faintest one
OwnTransponder::Emitter* OwnTransponder::emitter_for(uint32_t address, uint32_t now_ms) {
    Emitter* chosen = emitters_.data();
    for (Emitter& emitter : emitters_) {
        if (!emitter.used || !live(emitter, now_ms)) {
            chosen = &emitter;
            break;
        }
        if (emitter.level_sixteenths_dbm < chosen->level_sixteenths_dbm) chosen = &emitter;
    }
    *chosen = Emitter{};
    chosen->used = true;
    chosen->address = address;
    return chosen;
}

bool OwnTransponder::live(const Emitter& emitter, uint32_t now_ms) {
    return emitter.used && now_ms - emitter.heard_ms <= kEmitterMaxAgeMs;
}

const OwnTransponder::Emitter* OwnTransponder::leader(uint32_t now_ms) const {
    const Emitter* loudest = nullptr;
    const Emitter* runner_up = nullptr;
    for (const Emitter& emitter : emitters_) {
        if (!live(emitter, now_ms)) continue;
        if (loudest == nullptr || emitter.level_sixteenths_dbm > loudest->level_sixteenths_dbm) {
            runner_up = loudest;
            loudest = &emitter;
        } else if (runner_up == nullptr ||
                   emitter.level_sixteenths_dbm > runner_up->level_sixteenths_dbm) {
            runner_up = &emitter;
        }
    }
    if (loudest == nullptr || loudest->level_sixteenths_dbm < kLoudFloorDbm * kSixteenths)
        return nullptr;
    if (runner_up != nullptr &&
        loudest->level_sixteenths_dbm - runner_up->level_sixteenths_dbm < kLeadDb * kSixteenths)
        return nullptr;
    return loudest;
}

}  // namespace skyblip::bravo
