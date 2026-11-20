#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/conn.h>

#include "core/bus/bus.h"
#include "core/events/link.h"
#include "ports/link.h"

namespace skyblip::platform::zephyr {

class Link : public ports::Link {
   public:
    Status begin();

    // INFO: fc 04aug26 Read from the connection every time rather than latched
    // at connect: bt_gatt_get_mtu() already tracks the exchange, so a central
    // that negotiates late (iOS exchanges after it has discovered the service)
    // cannot leave a stale figure behind. The unit asks for one too, at
    // connect - see core/comms/README.md.
    uint16_t payload_bytes() const override;
    uint16_t payload_bytes_to(uint16_t session_id) const override;
    uint32_t connection(uint16_t session_id) const override;
    Status send(events::Endpoint endpoint, ConstByteSpan bytes) override;
    Status send_to(const events::Peer& peer, events::Endpoint endpoint,
                   ConstByteSpan bytes) override;
    Status advertise_as(const char* name) override;
    Status reserve_peer_connection(bool reserved) override;
    Status reserve_peer_connection_for(const events::BluetoothAddress& central) override;
    bool central_of(const events::Peer& peer, events::BluetoothAddress& out) const override;
    int phones() const override;

    // INFO: fc 19nov26 before begin(): the callbacks push frames and link events onto this bus
    void deliver_to(bus::Bus& bus);

   private:
    static Status advertise_within_ceiling(Status status, bool admits);
    static Status notify_one(struct bt_conn* connection, events::Endpoint endpoint,
                             ConstByteSpan bytes);
};

Link& link();

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
