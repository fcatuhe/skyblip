#ifndef SKYBLIP_CORE_COMMS_IDENTITY_LINK_H
#define SKYBLIP_CORE_COMMS_IDENTITY_LINK_H

#include <cstdint>
#include <optional>

#include "core/identity/messages.h"
#include "core/identity/unit.h"

namespace skyblip::comms {

class IdentityLink {
   public:
    // INFO: fc 18oct26 #126: long enough to reach the server and back, short of a forgotten tab
    static constexpr uint32_t kNonceForgetMs = 10u * 60u * 1000u;
    // INFO: fc 18oct26 the widest registration frame: version 9.99.99+99999, release false
    static constexpr int kRegistrationFrameBytes = 345;

    explicit IdentityLink(identity::Unit& unit) : unit_(unit) {}

    identity::Unit& unit() { return unit_; }
    bool activated() const { return unit_.activation().activated(); }

    bool open(uint16_t session_id, uint32_t now_ms);
    bool live(uint16_t session_id, uint32_t now_ms) const {
        return open_ && session_id_ == session_id && now_ms - opened_ms_ < kNonceForgetMs;
    }
    const identity::Nonce& unit_nonce() const { return unit_nonce_; }

    void hold_server_nonce(const identity::Nonce& server_nonce) {
        server_nonce_ = server_nonce;
        registering_ = true;
    }
    bool registering(uint16_t session_id, uint32_t now_ms) const {
        return live(session_id, now_ms) && registering_;
    }
    const identity::Nonce& server_nonce() const { return server_nonce_; }

    void close() {
        open_ = false;
        registering_ = false;
    }
    // INFO: fc 18oct26 live() holds the deadline, this keeps it after milliseconds() wraps back
    // into it
    void tick(uint32_t now_ms) {
        if (open_ && now_ms - opened_ms_ >= kNonceForgetMs) close();
    }

    // INFO: fc 18oct26 a held unlock is its sender's: its answer must never reach another session
    void forget(uint16_t session_id) {
        if (session_id_ != session_id) return;
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
    uint16_t session_id_{0};
    bool open_{false};
    bool registering_{false};
};

}  // namespace skyblip::comms

#endif
