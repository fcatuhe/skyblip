#ifndef SKYBLIP_PORTS_LINK_H
#define SKYBLIP_PORTS_LINK_H

#include "core/events/central.h"
#include "core/events/link.h"
#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::ports {

// INFO: fc 04aug26 all BLE guarantees: the 23-byte default ATT_MTU less a 3-byte header
constexpr uint16_t kMinimumLinkPayloadBytes = 20;

// INFO: fc 25sep26 WouldBlock is a link at its share of notifications, and a reply waits this long
constexpr uint32_t kReplyWaitCeilingMs = 2000;

class Link {
   public:
    virtual ~Link() = default;

    // INFO: fc 04aug26 asked per frame, as a central may settle the MTU late, iOS often at 182 here
    //
    // INFO: fc 18sep26 With several centrals this is the smallest, one frame goes to all.
    virtual uint16_t payload_bytes() const { return kMinimumLinkPayloadBytes; }
    virtual uint16_t payload_bytes_to(uint16_t /*session_id*/) const { return payload_bytes(); }

    // INFO: fc 10nov26 the connection a session id names now, as RxFrame::connection numbers it,
    // 0 for none
    virtual uint32_t connection(uint16_t /*session_id*/) const { return 0; }

    // INFO: fc 04aug26 longer than payload_bytes() is refused, as a controller fails it
    virtual Status send(events::Endpoint endpoint, ConstByteSpan bytes) = 0;

    // INFO: fc 18sep26 send() is every subscribed central, send_to() is the one that asked.
    // | 10nov26 Down once that connection is gone, even if its session id came back for another
    // | 26nov26 send() answers Unsubscribed when centrals are connected but none subscribed
    virtual Status send_to(const events::Peer& peer, events::Endpoint endpoint,
                           ConstByteSpan bytes) = 0;

    // INFO: fc 18oct26 starts advertising under name, or renames a link already advertising
    virtual Status advertise_as(const char* name) = 0;

    // INFO: fc 11nov26 keeps one connection for the paired peer, Full while phones hold them all
    virtual Status reserve_peer_connection(bool reserved) = 0;

    // INFO: fc 11nov26 Bravo's side: the kept connection admits the central at that address alone
    virtual Status reserve_peer_connection_for(const events::BluetoothAddress& central) = 0;

    // INFO: fc 11nov26 the address of the central on that connection, false once it is gone
    virtual bool central_of(const events::Peer& peer, events::BluetoothAddress& out) const = 0;

    // INFO: fc 12nov26 the centrals connected now, less the paired peer's kept connection
    virtual int phones() const = 0;
};

}

#endif
