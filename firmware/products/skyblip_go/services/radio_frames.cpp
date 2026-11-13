#include "core/protocol/callsign.h"
#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

// Two clocks meet here. The TimeStamp field counts quarter seconds from the top
// of the UTC second the slot belongs to, which the latched PPS edge anchors;
// the position has to be carried over the interval since the fix, which is a
// monotonic one. The dwell is armed before the slot opens, so the second figure
// is the staleness already accrued plus the whole wait still to come.
protocol::BurstInstant RadioService::burst_instant(const timing::Transmitter::Attempt& attempt,
                                                   uint64_t tx_at_us, uint32_t utc_s) const {
    const uint32_t tx_ms = static_cast<uint32_t>(tx_at_us / 1000);
    protocol::BurstInstant instant{};
    instant.utc_s = utc_s;
    instant.into_utc_ms = attempt.at_ms;
    instant.since_fix_ms = static_cast<int32_t>(tx_ms - context_.state.own.fix_at_ms);
    return instant;
}

uint8_t RadioService::encode(const timing::Transmitter::Attempt& attempt, uint64_t tx_at_us,
                             uint32_t utc_s, uint8_t* chips) {
    // INFO: fc 18oct26 no frame, no chips: the encoder refuses on its own, whatever attempt() said
    const settings::AirAddress address = context_.state.activation.address();
    bool built = false;
    switch (attempt.payload) {
        case Payload::Position:
            built = protocol::from_own(
                outgoing_, context_.state.own, address, settings::kAddressMappingTable,
                context_.state.own.aircraft_category, burst_instant(attempt, tx_at_us, utc_s));
            break;
        case Payload::Status:
            built = protocol::from_own_status(outgoing_, address, settings::kAddressMappingTable,
                                              status_report());
            break;
        case Payload::Callsign:
            built = protocol::from_own_callsign(outgoing_, address, settings::kAddressMappingTable,
                                                settings_.callsign);
            break;
    }
    if (!built) return 0;
    outgoing_.scramble();
    outgoing_.set_crc();
    return static_cast<uint8_t>(
        protocol::m_band_payload(adsl::kSyncWord, outgoing_.data, adsl::kFrameBytes, chips));
}

// INFO: fc 13nov26 G.2.6 leaves an unknown capability unset, so ADS-B counts only while Bravo is
// live
adsl::StatusReport RadioService::status_report() const {
    adsl::StatusReport report{};
    report.m_band = adsl::receive_class(timing::Scheduler::direct_heard_permille());
    report.conspicuity_received = adsl::conspicuity::kFlarm;
    if (context_.state.bravo == comms::BravoState::Live)
        report.conspicuity_received |= adsl::conspicuity::kAdsb1090;
    report.traffic_uplink_client = true;
    return report;
}

}  // namespace skyblip::go
