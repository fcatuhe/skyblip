#ifndef SKYBLIP_CORE_COMMS_IDENTITY_LINK_H
#define SKYBLIP_CORE_COMMS_IDENTITY_LINK_H

#include <cstdint>

#include "core/identity/messages.h"
#include "core/identity/unit.h"

namespace skyblip::comms {

class IdentityLink {
   public:
    // INFO: fc 18oct26 #126: long enough to reach the server and back, short of a forgotten tab
    static constexpr uint32_t kNonceForgetMs = 10u * 60u * 1000u;
    // INFO: fc 18oct26 the registration frame at the widest version dfu formats, 9.99.99+99999
    static constexpr int kRegistrationFrameBytes = 326;

    IdentityLink(identity::Unit& unit, bool complete) : unit_(unit), complete_(complete) {}

    identity::Unit& unit() { return unit_; }
    bool complete() const { return complete_ && unit_.keyed(); }

    bool open(uint16_t session_id, uint32_t now_ms);
    bool live(uint16_t session_id) const { return open_ && session_id_ == session_id; }
    const identity::Nonce& unit_nonce() const { return unit_nonce_; }

    void hold_server_nonce(const identity::Nonce& server_nonce) {
        server_nonce_ = server_nonce;
        registering_ = true;
    }
    bool registering(uint16_t session_id) const { return live(session_id) && registering_; }
    const identity::Nonce& server_nonce() const { return server_nonce_; }

    void close() {
        open_ = false;
        registering_ = false;
    }
    void forget(uint16_t session_id) {
        if (live(session_id)) close();
    }
    void tick(uint32_t now_ms) {
        if (open_ && now_ms - opened_ms_ >= kNonceForgetMs) close();
    }

   private:
    identity::Unit& unit_;
    identity::Nonce unit_nonce_{};
    identity::Nonce server_nonce_{};
    uint32_t opened_ms_{0};
    uint16_t session_id_{0};
    bool complete_;
    bool open_{false};
    bool registering_{false};
};

}  // namespace skyblip::comms

#endif
