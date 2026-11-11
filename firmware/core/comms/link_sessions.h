#ifndef SKYBLIP_CORE_COMMS_LINK_SESSIONS_H
#define SKYBLIP_CORE_COMMS_LINK_SESSIONS_H

#include "core/events/link.h"
#include "core/util/fifo.h"
#include "ports/link.h"

namespace skyblip::comms {

class LinkSessions {
   public:
    // INFO: fc 18sep26 Never above CONFIG_BT_MAX_CONN, the controller is what admits a central.
    static constexpr size_t kMaxSessions = 3;
    static constexpr size_t kMaxEvents = 8;
    static constexpr int kPeerConnections = 1;

    bool connected(uint16_t session_id, uint16_t payload_bytes) {
        Slot* slot = find(session_id);
        const uint32_t connection = slot == nullptr ? 0 : slot->connection;
        if (slot == nullptr && admits()) slot = free_slot();
        if (slot == nullptr) {
            refused_++;
            return false;
        }
        *slot = Slot{};
        slot->used = true;
        slot->session_id = session_id;
        slot->connection = connection != 0 ? connection : next_connection();
        slot->payload_bytes = floor_payload(payload_bytes);
        slot->mtu_updates = slot->payload_bytes > ports::kMinimumLinkPayloadBytes ? 1 : 0;
        raise(events::LinkEventType::Up, *slot);
        return true;
    }

    // INFO: fc 11nov26 a paired Bravo keeps its connection while away, so phones get one fewer
    Status reserve_peer(bool reserved) {
        if (reserved && !peer_reserved_ && count() >= static_cast<int>(kMaxSessions))
            return Status::Full;
        peer_reserved_ = reserved;
        return Status::Ok;
    }

    bool peer_reserved() const { return peer_reserved_; }

    int phone_ceiling() const {
        return static_cast<int>(kMaxSessions) - (peer_reserved_ ? kPeerConnections : 0);
    }

    bool admits() const { return count() < phone_ceiling(); }

    // INFO: le 04aug26 The MTU exchange lands late, so this is a second Up on one session.
    void payload_changed(uint16_t session_id, uint16_t payload_bytes) {
        Slot* slot = find(session_id);
        if (slot == nullptr) return;
        if (slot->mtu_updates < UINT8_MAX) slot->mtu_updates++;
        const uint16_t figure = floor_payload(payload_bytes);
        if (figure == slot->payload_bytes) return;
        slot->payload_bytes = figure;
        raise(events::LinkEventType::Up, *slot);
    }

    void mtu_requested(uint16_t session_id, events::MtuRequest outcome, uint8_t = 0) {
        Slot* slot = find(session_id);
        if (slot != nullptr) slot->exchanged_by_unit = outcome == events::MtuRequest::Exchanged;
    }

    void disconnected(uint16_t session_id) {
        Slot* slot = find(session_id);
        if (slot == nullptr) return;
        slot->used = false;
        raise(events::LinkEventType::Down, *slot);
    }

    bool pop(events::LinkEvent& out) {
        Result<events::LinkEvent> event = events_.pop();
        if (!event.ok()) return false;
        out = event.value();
        return true;
    }

    bool up() const { return count() > 0; }
    bool up(uint16_t session_id) const { return find(session_id) != nullptr; }

    // INFO: fc 10nov26 never 0 and never given twice before 2^32 connections, unlike a session id
    uint32_t connection(uint16_t session_id) const {
        const Slot* slot = find(session_id);
        return slot == nullptr ? 0 : slot->connection;
    }

    int count() const {
        int sessions = 0;
        for (const Slot& slot : slots_)
            if (slot.used) sessions++;
        return sessions;
    }

    uint16_t payload_bytes() const {
        uint16_t smallest = 0;
        for (const Slot& slot : slots_)
            if (slot.used && (smallest == 0 || slot.payload_bytes < smallest))
                smallest = slot.payload_bytes;
        return smallest == 0 ? ports::kMinimumLinkPayloadBytes : smallest;
    }

    uint16_t payload_bytes(uint16_t session_id) const {
        const Slot* slot = find(session_id);
        return slot == nullptr ? ports::kMinimumLinkPayloadBytes : slot->payload_bytes;
    }

    uint32_t dropped() const { return dropped_; }
    uint32_t refused() const { return refused_; }

   private:
    struct Slot {
        uint32_t connection{0};
        uint16_t session_id{0};
        uint16_t payload_bytes{ports::kMinimumLinkPayloadBytes};
        uint8_t mtu_updates{0};
        bool exchanged_by_unit{false};
        bool used{false};
    };

    static events::MtuExchangeStarter starter_of(const Slot& slot) {
        const bool by_central = slot.mtu_updates > (slot.exchanged_by_unit ? 1 : 0);
        if (by_central && slot.exchanged_by_unit) return events::MtuExchangeStarter::Both;
        if (by_central) return events::MtuExchangeStarter::Central;
        if (slot.exchanged_by_unit) return events::MtuExchangeStarter::Unit;
        return events::MtuExchangeStarter::None;
    }

    uint32_t next_connection() {
        if (++last_connection_ == 0) ++last_connection_;
        return last_connection_;
    }

    static uint16_t floor_payload(uint16_t bytes) {
        return bytes < ports::kMinimumLinkPayloadBytes ? ports::kMinimumLinkPayloadBytes : bytes;
    }

    Slot* find(uint16_t session_id) {
        for (Slot& slot : slots_)
            if (slot.used && slot.session_id == session_id) return &slot;
        return nullptr;
    }

    const Slot* find(uint16_t session_id) const {
        for (const Slot& slot : slots_)
            if (slot.used && slot.session_id == session_id) return &slot;
        return nullptr;
    }

    Slot* free_slot() {
        for (Slot& slot : slots_)
            if (!slot.used) return &slot;
        return nullptr;
    }

    void raise(events::LinkEventType type, const Slot& slot) {
        events::LinkEvent event{};
        event.type = type;
        event.session_id = slot.session_id;
        event.payload_bytes = slot.payload_bytes;
        event.connection = slot.connection;
        if (type == events::LinkEventType::Down) event.mtu_exchange_started_by = starter_of(slot);
        if (!is_ok(events_.push(event))) dropped_++;
    }

    Fifo<events::LinkEvent, kMaxEvents> events_{};
    Slot slots_[kMaxSessions]{};
    uint32_t last_connection_{0};
    uint32_t dropped_{0};
    uint32_t refused_{0};
    bool peer_reserved_{false};
};

}

#endif
