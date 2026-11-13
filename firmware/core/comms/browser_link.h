#ifndef SKYBLIP_CORE_COMMS_BROWSER_LINK_H
#define SKYBLIP_CORE_COMMS_BROWSER_LINK_H

#include <cstdint>
#include <cstring>

#include "core/comms/trusted_browsers.h"
#include "core/events/link.h"
#include "core/identity/messages.h"

namespace skyblip::comms {

// INFO: fc 13nov26 the nonce, proof and trust are the claim holder's, and its disconnect ends them
class BrowserLink {
   public:
    static constexpr int kChallengeFrameBytes = 22;
    static constexpr int kEntryFrameBytes = 45;

    TrustedBrowsers& trusted() { return trusted_; }
    const TrustedBrowsers& trusted() const { return trusted_; }

    void open(const events::Peer& peer, const identity::Nonce& nonce) {
        forget(peer_.session_id);
        peer_ = peer;
        nonce_ = nonce;
        nonce_live_ = true;
    }
    bool spend_nonce(const events::Peer& peer, identity::Nonce& out) {
        if (!nonce_live_ || !(peer == peer_)) return false;
        nonce_live_ = false;
        out = nonce_;
        return true;
    }

    void prove(const crypto::Ed25519::PublicKey& key, const char* label) {
        key_ = key;
        std::memcpy(label_, label, sizeof(label_));
        proven_ = true;
        trusted_session_ = trusted_.holds(key);
    }
    bool trusts(const events::Peer& peer) const { return trusted_session_ && peer == peer_; }

    // INFO: fc 13nov26 two presses on a set or a dfu are what turn a proven key into a trusted one
    bool remember(const events::Peer& peer) {
        if (!proven_ || !(peer == peer_) || trusted_.holds(key_)) return false;
        trusted_.store(key_, label_);
        trusted_session_ = true;
        return true;
    }

    bool forget_browser(const BrowserId& id) {
        if (!trusted_.forget(id)) return false;
        if (proven_ && !trusted_.holds(key_)) {
            proven_ = false;
            trusted_session_ = false;
        }
        return true;
    }

    void forget(uint16_t session_id) {
        if (session_id != peer_.session_id) return;
        peer_ = events::Peer{0, 0};
        nonce_live_ = false;
        proven_ = false;
        trusted_session_ = false;
    }

   private:
    TrustedBrowsers trusted_{};
    identity::Nonce nonce_{};
    crypto::Ed25519::PublicKey key_{};
    char label_[kBrowserLabelBytes + 1]{};
    events::Peer peer_{0, 0};
    bool nonce_live_{false};
    bool proven_{false};
    bool trusted_session_{false};
};

}  // namespace skyblip::comms

#endif
