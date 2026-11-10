#ifndef SKYBLIP_CORE_COMMS_LINK_CLAIM_H
#define SKYBLIP_CORE_COMMS_LINK_CLAIM_H

#include <cstdint>

#include "core/events/link.h"

namespace skyblip::comms {

class LinkClaim {
   public:
    bool grant(const events::Peer& peer) {
        if (!held_) {
            held_ = true;
            holder_ = peer;
        }
        return holder_ == peer;
    }

    bool holds(uint16_t session_id) const { return held_ && holder_.session_id == session_id; }
    bool holds(const events::Peer& peer) const { return held_ && holder_ == peer; }
    bool held() const { return held_; }
    uint16_t holder() const { return holder_.session_id; }
    const events::Peer& peer() const { return holder_; }

    void release(uint16_t session_id) {
        if (holds(session_id)) release();
    }

    void release() {
        held_ = false;
        holder_ = events::Peer{0, 0};
    }

   private:
    events::Peer holder_{0, 0};
    bool held_{false};
};

}

#endif
