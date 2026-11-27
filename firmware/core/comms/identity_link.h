#ifndef SKYBLIP_CORE_COMMS_IDENTITY_LINK_H
#define SKYBLIP_CORE_COMMS_IDENTITY_LINK_H

#include <cstdint>
#include <optional>

#include "core/events/link.h"
#include "core/identity/messages.h"
#include "core/identity/unit.h"

namespace skyblip::comms {

class IdentityLink {
   public:
    // INFO: fc 18oct26 #126: long enough to reach the server and back, short of a forgotten tab
    static constexpr uint32_t kNonceForgetMs = 10u * 60u * 1000u;
    // INFO: fc 18oct26 the widest registration frame: version 9.99.99+99999 | 25nov26 tag over 0xFF
    static constexpr int kRegistrationFrameBytes = 146;
    static constexpr int kChallengeFrameBytes = 27;
    // INFO: fc 14nov26 the widest attestation frame: version 9.99.99+99999
    static constexpr int kAttestationFrameBytes = 102;
    static constexpr int kIdentityFrameBytes = 39;

    explicit IdentityLink(identity::Unit& unit) : unit_(unit) {}

    identity::Unit& unit() { return unit_; }
    bool activated() const { return unit_.activation().activated(); }

    bool open(const events::Peer& peer, uint32_t now_ms) {
        close();
        if (!unit_.draw_nonce(unit_nonce_)) return false;
        peer_ = peer;
        opened_ms_ = now_ms;
        open_ = true;
        return true;
    }
    bool live(const events::Peer& peer, uint32_t now_ms) const {
        return open_ && peer_ == peer && now_ms - opened_ms_ < kNonceForgetMs;
    }
    const identity::Nonce& unit_nonce() const { return unit_nonce_; }

    void hold_server_nonce(const identity::Nonce& server_nonce) {
        server_nonce_ = server_nonce;
        registering_ = true;
    }
    bool registering(const events::Peer& peer, uint32_t now_ms) const {
        return live(peer, now_ms) && registering_;
    }
    const identity::Nonce& server_nonce() const { return server_nonce_; }

    void close() {
        open_ = false;
        registering_ = false;
    }
    // INFO: fc 18oct26 live() holds the deadline, this keeps it once milliseconds() wraps
    void tick(uint32_t now_ms) {
        if (open_ && now_ms - opened_ms_ >= kNonceForgetMs) close();
    }

    // INFO: fc 18oct26 a held unlock is its sender's: its answer must never reach another session
    void forget(const events::Peer& peer) {
        if (peer_ != peer) return;
        close();
        unlock_due_.reset();
    }

    // INFO: fc 18oct26 verified and its nonce spent, waiting for the product to place the write
    void hold_unlock(const identity::UnlockCode& code) { unlock_due_ = code; }
    const std::optional<identity::UnlockCode>& unlock_due() const { return unlock_due_; }
    void unlock_placed() { unlock_due_.reset(); }

   private:
    identity::Unit& unit_;
    identity::Nonce unit_nonce_{};
    identity::Nonce server_nonce_{};
    uint32_t opened_ms_{0};
    std::optional<identity::UnlockCode> unlock_due_{};
    events::Peer peer_{0, 0};
    bool open_{false};
    bool registering_{false};
};

}  // namespace skyblip::comms

#endif
