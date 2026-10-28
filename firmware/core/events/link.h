#ifndef SKYBLIP_CORE_EVENTS_LINK_H
#define SKYBLIP_CORE_EVENTS_LINK_H

#include <array>
#include <cstdint>

namespace skyblip::events {
// Log is its own endpoint and not a verb on Config: an offload is thousands of
// round trips and would otherwise sit in the same queue as the prompt that
// authorises a firmware upload.
enum class Endpoint : uint8_t { Config = 0, Nmea = 1, Log = 2 };

// INFO: fc 04aug26 Payload, not MTU: the two differ by the three bytes of
// notification header, which is exactly the off-by-three that makes a frame an
// iPhone refuses. ports::Link::payload_bytes() is the authority because it stays
// current through a late MTU exchange; this field is the same number at the
// moment the link came up, in the same unit, so the tree carries one figure.
struct LinkUp {
    uint16_t session_id;
    uint16_t payload_bytes;
};

enum class MtuRequest : uint8_t { NotAsked, Pending, Exchanged, AlreadyExchanged, Refused, Unsent };

enum class MtuExchangeStarter : uint8_t { None = 0, Central = 1, Unit = 2, Both = 3 };

struct LinkDown {
    uint16_t session_id;
};

// INFO: le 04aug26 One tagged event on one queue, not a queue of LinkUp beside a
// queue of LinkDown: a connection is an ordered lifecycle, and an Up read before
// the Down that came first leaves a service pushing at a link that is gone. Two
// queues cannot promise that order. On Down, payload_bytes is what the link settled on.
enum class LinkEventType : uint8_t { Up, Down };

struct LinkEvent {
    LinkEventType type;
    MtuExchangeStarter mtu_exchange_started_by;
    uint16_t session_id;
    uint16_t payload_bytes;
};

struct RxFrame {
    uint16_t session_id;
    Endpoint endpoint;
    uint16_t bytes;
    std::array<uint8_t, 256> data;
};

struct DfuRequest {
    uint16_t session_id;
};

}  // namespace skyblip::events

#endif
