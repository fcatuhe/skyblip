#include "products/skyblip_bravo/pairing.h"

namespace skyblip::bravo {

Heard Pairing::hear(const events::BluetoothAddress& go, bool go_paired, bool new_connection,
                    uint32_t now_ms) {
    tick(now_ms);
    if (pinned_to(go)) {
        if (go_paired) return Heard::Pinned;
        // INFO: fc 11nov26 the Go unpaired, so Bravo drops its own half
        pin_ = PeerPin{};
        return Heard::Unpinned;
    }
    // INFO: fc 11nov26 an offered Go asking on a new connection gave up, so it needs a new press
    if (offered_to(go) && !go_paired && new_connection) offer_.reset();
    if (offered_to(go)) {
        if (!go_paired) return Heard::Awaited;
        pin_ = pin_of(go);
        offer_.reset();
        if (request_ && request_->go == go) request_.reset();
        return Heard::Committed;
    }
    return go_paired ? Heard::Stranger : Heard::Asking;
}

bool Pairing::ask(const events::BluetoothAddress& go, uint32_t now_ms) {
    tick(now_ms);
    if (request_) return false;
    request_ = Ask{go, now_ms};
    return true;
}

bool Pairing::answer(uint32_t now_ms) {
    tick(now_ms);
    if (!request_) return false;
    offer_ = Ask{request_->go, now_ms};
    request_.reset();
    return true;
}

void Pairing::withdraw(const events::BluetoothAddress& go) {
    if (request_ && request_->go == go) request_.reset();
}

void Pairing::tick(uint32_t now_ms) {
    if (request_ && now_ms - request_->since_ms >= kRequestWindowMs) request_.reset();
    if (offer_ && now_ms - offer_->since_ms >= kOfferWindowMs) offer_.reset();
}

bool Pairing::pinned_to(const events::BluetoothAddress& go) const {
    return pin_.pinned && address_of(pin_) == go;
}

bool Pairing::offered_to(const events::BluetoothAddress& go) const {
    return offer_ && offer_->go == go;
}

}  // namespace skyblip::bravo
