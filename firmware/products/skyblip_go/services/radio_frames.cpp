#include "core/protocol/callsign.h"
#include "products/skyblip_go/services/radio.h"

namespace skyblip::go {

uint8_t RadioService::encode(const timing::Transmitter::Attempt& attempt, uint64_t tx_at_us,
                             uint32_t utc_s, uint8_t* chips) {
    // INFO: fc 18oct26 no frame, no chips: the encoder refuses on its own, whatever attempt() said
    const settings::OnAir sender =
        settings::on_air(context_.state.activation.address(), settings_.aircraft_address);
    bool built = false;
    switch (attempt.payload) {
        case Payload::Position:
            built = protocol::from_own(outgoing_, context_.state.own, sender.address,
                                       sender.address_mapping_table,
                                       context_.state.own.aircraft_category);
            if (!built && sender.address) hold_for_fix(utc_s);
            if (built &&
                outgoing_.protocol_version() >
                    context_.state.traffic.readable_version(
                        context_.state.own, static_cast<uint32_t>(tx_at_us / 1000)) &&
                !protocol::issue1_form(outgoing_, context_.state.own)) {
                built = false;
                if (!held_for_b5_ || held_for_b5_s_ != utc_s) context_.state.air.tx_held_for_b5++;
                held_for_b5_ = true;
                held_for_b5_s_ = utc_s;
            }
            break;
        case Payload::Status:
            built = protocol::from_own_status(outgoing_, sender.address,
                                              sender.address_mapping_table, status_report());
            break;
        case Payload::Callsign:
            built = protocol::from_own_callsign(outgoing_, sender.address,
                                                sender.address_mapping_table, settings_.callsign);
            break;
    }
    if (!built) return 0;
    const uint8_t bytes = outgoing_.data_bytes();
    outgoing_.scramble(bytes);
    outgoing_.set_crc(bytes);
    wire::manchester_encode(&bytes, 1, chips);
    wire::manchester_encode(outgoing_.data, bytes, chips + 2);
    return static_cast<uint8_t>(2 + 2 * bytes);
}

// INFO: fc 13nov26 G.2.6 leaves an unknown capability unset: ADS-B counts while Bravo is live
adsl::StatusReport RadioService::status_report() const {
    adsl::StatusReport report{};
    report.m_band = adsl::receive_class(timing::Scheduler::direct_heard_permille());
    report.conspicuity_received = adsl::conspicuity::kFlarm;
    if (context_.state.bravo == comms::BravoState::Live)
        report.conspicuity_received |= adsl::conspicuity::kAdsb1090;
    // INFO: fc 19nov26 Bravo's service runs after this one, so an address changed this pass is
    // unconfirmed
    const bus::AircraftTransponder& transponder = context_.state.aircraft_transponder;
    if (transponder.address != 0 && transponder.address == settings_.aircraft_address) {
        report.transponder = adsl::Transponder::ModeS;
        if (transponder.adsb_out) report.conspicuity_transmitted = adsl::conspicuity::kAdsb1090;
    }
    report.traffic_uplink_client = true;
    return report;
}

}  // namespace skyblip::go
