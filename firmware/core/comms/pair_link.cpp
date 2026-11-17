#include <cstdint>

#include "core/comms/config.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

// INFO: fc 16nov26 Bravo has no glass, so only a browser it trusts may arm its press
void ConfigService::request_pairing(const cbor::Reader& frame) {
    if (in_flight()) {
        refuse(link::Reason::InFlight);
        return;
    }
    if (pairing_side_ == PairingSide::Bravo && !trusted_browser()) {
        refuse(link::Reason::Untrusted);
        return;
    }
    const Result<uint32_t> tag = frame.read_unsigned(link::pair_key::kTag);
    if (!tag.ok()) {
        refuse(link::Reason::Invalid);
        return;
    }
    if (tag.value() > UINT16_MAX) {
        refuse(link::Reason::OutOfRange);
        return;
    }
    if (paired_tag_ == tag.value()) {
        acknowledge(true, link::Reason::Paired);
        return;
    }
    if (trusted_browser()) {
        start_pairing(static_cast<uint16_t>(tag.value()));
        return;
    }
    staged_pair_tag_ = static_cast<uint16_t>(tag.value());
    stage(Pending::Pair, link::Reason::ConfirmPair);
    (void)format_pair_detail(prompt_detail_, staged_pair_tag_);
}

void ConfigService::start_pairing(uint16_t tag) {
    pair_requested_ = tag;
    pairing_ended(link::Reason::Cancelled);
    if (pairing_side_ == PairingSide::Go) pairing_peer_ = claim_.peer();
    acknowledge(true, link::Reason::Pair);
}

std::optional<uint16_t> ConfigService::take_pair_request() {
    const std::optional<uint16_t> tag = pair_requested_;
    pair_requested_.reset();
    return tag;
}

void ConfigService::pairing_ended(link::Reason verdict) {
    if (!pairing_peer_) return;
    const events::Peer peer = *pairing_peer_;
    pairing_peer_.reset();
    if (link_.connection(peer.session_id) != peer.connection) return;
    acknowledge_to(peer, verdict == link::Reason::Paired, verdict);
}

}  // namespace skyblip::comms
