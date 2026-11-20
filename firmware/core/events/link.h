#ifndef SKYBLIP_CORE_EVENTS_LINK_H
#define SKYBLIP_CORE_EVENTS_LINK_H

#include <array>
#include <cstdint>

namespace skyblip::events {
// INFO: fc 20nov26 Log is its own endpoint so an offload never queues beside an upload prompt
enum class Endpoint : uint8_t { Config = 0, Nmea = 1, Log = 2, Peer = 3 };

// INFO: fc 04aug26 payload, not MTU: the 3-byte header between them is what iPhones refuse
struct LinkUp {
    uint16_t session_id;
    uint16_t payload_bytes;
    uint32_t connection;
};

// INFO: fc 10nov26 a reply goes to the connection that asked: silicon reuses session ids
struct Peer {
    uint16_t session_id;
    uint32_t connection;
};

inline bool operator==(const Peer& a, const Peer& b) {
    return a.session_id == b.session_id && a.connection == b.connection;
}
inline bool operator!=(const Peer& a, const Peer& b) { return !(a == b); }

enum class MtuRequest : uint8_t { NotAsked, Pending, Exchanged, AlreadyExchanged, Refused, Unsent };

enum class MtuExchangeStarter : uint8_t { None = 0, Central = 1, Unit = 2, Both = 3 };

// INFO: fc 19nov26 reason is the HCI code the controller gave, 0 where the platform names none
struct LinkDown {
    uint16_t session_id;
    uint8_t reason{0};
};

// INFO: le 04aug26 Up and Down share one queue, because two queues cannot keep a connection's order
enum class LinkEventType : uint8_t { Up, Down };

struct LinkEvent {
    LinkEventType type;
    MtuExchangeStarter mtu_exchange_started_by;
    uint16_t session_id;
    uint16_t payload_bytes;
    uint32_t connection;
    uint8_t reason{0};
};

struct RxFrame {
    uint16_t session_id;
    // INFO: fc 10nov26 LinkSessions numbers each connection once: silicon reuses session ids
    uint32_t connection;
    Endpoint endpoint;
    uint16_t bytes;
    std::array<uint8_t, 256> data;
};

inline Peer peer_of(const RxFrame& frame) { return Peer{frame.session_id, frame.connection}; }

struct DfuRequest {
    uint16_t session_id;
};

}  // namespace skyblip::events

#endif
