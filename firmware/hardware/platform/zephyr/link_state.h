#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_STATE_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_LINK_STATE_H
#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "core/comms/link_sessions.h"
#include "core/events/link.h"

// INFO: fc 18sep26 Nordic's roles: the app writes RX, we notify TX (XCSoar BleSerialPort.java:100).
#define NUS_UUID(id) BT_UUID_128_ENCODE(0x6e400000 | (id), 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)

namespace skyblip::platform::zephyr::link_state {

inline bus::Bus* g_bus = nullptr;
// INFO: fc 19nov26 every callback pushes onto the bus under this lock, so the stack is one producer
inline struct k_spinlock g_lock;
inline comms::LinkSessions g_sessions;

// INFO: fc 11nov26 the Go's own link to its Bravo is central.cpp's: no session, no NMEA, no MTU ask
inline bool phone(struct bt_conn* connection) {
    struct bt_conn_info info = {};
    return bt_conn_get_info(connection, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL;
}

// INFO: fc 18sep26 bt_conn_index() is the controller's own slot, so session ids never collide.
inline uint16_t session_of(struct bt_conn* connection) {
    return static_cast<uint16_t>(bt_conn_index(connection) + 1);
}

constexpr int kMaxNotifyAttributes = 2;
int notify_attributes(events::Endpoint endpoint, const struct bt_gatt_attr** out);

void resume_advertising();

}  // namespace skyblip::platform::zephyr::link_state

#endif  // __ZEPHYR__
#endif
