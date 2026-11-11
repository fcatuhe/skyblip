#include "products/skyblip_bravo/gate_reference.h"

#include <cstdint>

namespace skyblip::bravo {

namespace {

constexpr int64_t kSecondMs = 1000;
constexpr int64_t kLastMillisecondOfSecond = 999;

}  // namespace

// INFO: fc 11nov26 the claim's earliest instant against Bravo's latest, so an unknown phase ages it
void GateReference::hear(const comms::PeerClaim& claim, const diagnostics::Instant& at,
                         uint32_t now_ms) {
    claim_ = claim;
    heard_ = true;
    heard_ms_ = now_ms;
    go_own_icao_ = claim.own_icao;
    transit_ms_ = 0;
    if (!claim.utc_valid || !at.utc_dated) return;
    const int64_t written_ms =
        static_cast<int64_t>(claim.at_s) * kSecondMs + (claim.phase_valid ? claim.into_ms : 0);
    const int64_t read_ms = static_cast<int64_t>(at.at_s) * kSecondMs +
                            (at.phase_valid ? at.into_ms : kLastMillisecondOfSecond);
    const int64_t transit_ms = read_ms - written_ms;
    if (transit_ms > 0)
        transit_ms_ = transit_ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(transit_ms);
}

void GateReference::unpair() {
    heard_ = false;
    go_own_icao_ = 0;
}

GateChoice GateReference::choose(const model::Ownship& own, uint32_t now_ms) const {
    if (!claim_.fix_valid || !fresh(claim_.fix_age_ms, now_ms))
        return GateChoice{
            own.fix_valid ? comms::ExportReference::Own : comms::ExportReference::None, own};
    GateChoice choice{comms::ExportReference::Go, model::Ownship{}};
    model::Ownship& go = choice.own;
    go.fix_valid = true;
    go.utc_valid = own.utc_valid;
    go.utc_s = own.utc_s;
    go.latitude_1e7 = claim_.latitude_1e7;
    go.longitude_1e7 = claim_.longitude_1e7;
    go.fix_at_ms = heard_ms_ - transit_ms_ - claim_.fix_age_ms;
    go.altitude_hae_valid = claim_.altitude_hae_valid && fresh(claim_.altitude_hae_age_ms, now_ms);
    if (go.altitude_hae_valid) go.altitude_hae_mm = claim_.altitude_hae_mm;
    go.pressure_altitude_valid =
        claim_.pressure_altitude_valid && fresh(claim_.pressure_altitude_age_ms, now_ms);
    if (go.pressure_altitude_valid) {
        go.pressure_altitude_mm = claim_.pressure_altitude_mm;
        go.pressure_altitude_at_ms = heard_ms_ - transit_ms_ - claim_.pressure_altitude_age_ms;
    }
    return choice;
}

uint32_t GateReference::own_icao(uint32_t bravo_own_icao) const {
    return go_own_icao_ != 0 ? go_own_icao_ : bravo_own_icao;
}

// INFO: fc 11nov26 a claim the Go wrote without UTC cannot be dated, so it never gates
bool GateReference::fresh(uint32_t age_ms, uint32_t now_ms) const {
    if (!heard_ || !claim_.utc_valid) return false;
    const uint64_t total_ms = static_cast<uint64_t>(age_ms) + transit_ms_ + (now_ms - heard_ms_);
    return total_ms <= kClaimMaxAgeMs;
}

}  // namespace skyblip::bravo
