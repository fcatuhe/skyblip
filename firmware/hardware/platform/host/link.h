#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_LINK_H

#include <string>
#include <vector>

#include "core/bus/bus.h"
#include "core/comms/link_sessions.h"
#include "core/events/link.h"
#include "ports/link.h"

namespace skyblip::platform::host {

class Link : public ports::Link {
   public:
    struct Frame {
        events::Endpoint endpoint;
        std::string bytes;
        // INFO: fc 18sep26 Session 0 is the broadcast, what silicon put on every connection.
        uint16_t session{0};
    };

    // INFO: fc 04aug26 A phone that negotiated ATT_MTU 247, which is what an
    // Android central and a nRF Connect both ask for. Cases that care about the
    // ends of the range say so with declare_payload_bytes().
    static constexpr uint16_t kDefaultPayloadBytes = 244;

    static Status begin() { return Status::Ok; }

    void push_rx(const events::RxFrame& frame) { rx_.push(frame); }
    bool pop_rx(events::RxFrame& out) { return rx_.pop(out); }

    void raise_link(uint16_t session_id = 1) {
        sessions_.connected(session_id, declared_);
        if (!sessions_.up(session_id)) return;
        live_.push_back(session_id);
        last_ = session_id;
    }
    void drop_link() { drop_link(last_); }
    void drop_link(uint16_t session_id) {
        sessions_.disconnected(session_id);
        for (size_t i = 0; i < live_.size(); i++) {
            if (live_[i] != session_id) continue;
            live_.erase(live_.begin() + static_cast<long>(i));
            break;
        }
    }
    bool pop_event(events::LinkEvent& out) { return sessions_.pop(out); }
    bool up() const { return sessions_.up(); }
    int links() const { return sessions_.count(); }
    uint16_t session_id() const { return last_; }

    // What this link came up with. Floored the way a real one is: nothing may
    // model a central that offers less than BLE guarantees. Said while a link is
    // up it is the MTU exchange landing late, which is what an iOS central does,
    // and it reaches the bus the same way silicon's att_mtu_updated does.
    // INFO: fc 18sep26 What every central here negotiates, said before or after one connects.
    void declare_payload_bytes(uint16_t bytes) {
        declared_ = bytes < ports::kMinimumLinkPayload ? ports::kMinimumLinkPayload : bytes;
        for (uint16_t session : live_) sessions_.payload_changed(session, declared_);
    }

    void declare_payload_bytes(uint16_t session_id, uint16_t bytes) {
        sessions_.payload_changed(session_id, bytes);
    }

    uint16_t payload_bytes() const override { return sessions_.payload_bytes(); }
    uint16_t payload_bytes_to(uint16_t session_id) const override {
        return sessions_.payload_bytes(session_id);
    }

    Status send(events::Endpoint ep, ConstByteSpan bytes) override { return record(0, ep, bytes); }

    Status send_to(uint16_t session_id, events::Endpoint ep, ConstByteSpan bytes) override {
        if (!sessions_.up(session_id)) return Status::Down;
        return record(session_id, ep, bytes);
    }

    // INFO: fc 25sep26 silicon's per-link notify share: past it WouldBlock, until serve()
    void hold_after(int frames) { share_ = frames; }
    void serve() { in_flight_ = 0; }

    void force_status(Status s, bool once = true) {
        next_status_ = s;
        once_ = once;
    }

    const Frame& last() const { return sent.back(); }
    bool last_on(events::Endpoint ep) const { return !sent.empty() && sent.back().endpoint == ep; }
    int count_on(events::Endpoint ep) const {
        int n = 0;
        for (const auto& f : sent)
            if (f.endpoint == ep) n++;
        return n;
    }
    int count_to(uint16_t session_id, events::Endpoint ep) const {
        int n = 0;
        for (const auto& f : sent)
            if (f.endpoint == ep && f.session == session_id) n++;
        return n;
    }
    void clear() { sent.clear(); }

    std::vector<Frame> sent;
    int refused_oversize{0};

   private:
    Status record(uint16_t session_id, events::Endpoint ep, ConstByteSpan bytes) {
        // The controller's refusal, modelled: an oversized notification is not
        // shortened, it fails, so no case can pass by sending one.
        if (bytes.size() > (session_id == 0 ? payload_bytes() : payload_bytes_to(session_id))) {
            refused_oversize++;
            return Status::OutOfRange;
        }
        if (next_status_ != Status::Ok) {
            Status s = next_status_;
            if (once_) next_status_ = Status::Ok;
            return s;
        }
        if (share_ > 0 && in_flight_ >= share_) return Status::WouldBlock;
        in_flight_++;
        sent.push_back({ep, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                        session_id});
        return Status::Ok;
    }

    bus::Queue<events::RxFrame, 4> rx_;
    comms::LinkSessions sessions_{};
    std::vector<uint16_t> live_;
    uint16_t declared_{kDefaultPayloadBytes};
    uint16_t last_{0};
    Status next_status_{Status::Ok};
    bool once_{true};
    int share_{0};
    int in_flight_{0};
};

}

#endif
