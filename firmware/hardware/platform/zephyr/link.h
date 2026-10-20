#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_H
#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/conn.h>

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
    Status send(events::Endpoint endpoint, ConstByteSpan bytes) override;
    Status send_to(uint16_t session_id, events::Endpoint endpoint, ConstByteSpan bytes) override;
    void note_reply_too_wide(uint16_t session_id) override;
    Status advertise_as(const char* name) override;

    // Non-blocking: pop one queued inbound frame (config writes). The shell
    // drains this into App::on_link_rx(). Returns false when empty.
    bool pop_rx(events::RxFrame& out);

    // The same handover for the connection itself: the Bluetooth callbacks fill
    // a comms::LinkSession, the board drains it onto bus.link_events beside the
    // frames. The board calls this on both platforms, so a port that stops
    // offering it stops building.
    bool pop_event(events::LinkEvent& out);

   private:
    static Status notify_one(struct bt_conn* connection, events::Endpoint endpoint,
                             ConstByteSpan bytes);
};

Link& link();

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
