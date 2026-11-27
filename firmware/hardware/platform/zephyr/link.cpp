#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/link.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "core/comms/link_sessions.h"
#include "core/events/link.h"
#include "hardware/platform/zephyr/att_budget.h"
#include "hardware/platform/zephyr/bluetooth_address.h"
#include "hardware/platform/zephyr/link_state.h"
#include "hardware/platform/zephyr/loop_wake.h"

namespace skyblip::platform::zephyr {

using link_state::g_bus;
using link_state::g_lock;
using link_state::g_sessions;
using link_state::kMaxNotifyAttributes;
using link_state::notify_attributes;
using link_state::phone;
using link_state::resume_advertising;
using link_state::session_of;
using skyblip::events::Endpoint;
using skyblip::events::LinkEvent;
using skyblip::events::MtuRequest;

namespace {

// INFO: fc 04aug26 opcode and value handle, so an iPhone's ATT_MTU of 185 carries 182 bytes of
// payload
constexpr uint16_t kNotifyHeaderBytes = 3;

struct bt_conn* g_connections[CONFIG_BT_MAX_CONN] = {};
// INFO: fc 11nov26 equal: every phone is a row, and a reserved Bravo finds a free connection
static_assert(CONFIG_BT_MAX_CONN == static_cast<int>(comms::LinkSessions::kMaxSessions),
              "the controller admits more phones than core will serve, or fewer than it reserves");

atomic_t g_in_flight[CONFIG_BT_MAX_CONN];

void notified(struct bt_conn* connection, void* /*user_data*/) {
    atomic_dec(&g_in_flight[bt_conn_index(connection)]);
}

// INFO: fc 23sep26 referenced under the callbacks' lock, so a released connection is never notified
struct bt_conn* connection_reference(size_t index) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    struct bt_conn* connection =
        g_connections[index] != nullptr ? bt_conn_ref(g_connections[index]) : nullptr;
    k_spin_unlock(&g_lock, key);
    return connection;
}

struct bt_conn* connection_of(uint16_t session_id) {
    if (session_id == 0 || session_id > ARRAY_SIZE(g_connections)) return nullptr;
    return connection_reference(session_id - 1);
}

// INFO: fc 10nov26 checked and referenced under one lock, so a reconnect cannot redirect a reply
struct bt_conn* connection_of(const skyblip::events::Peer& peer) {
    if (peer.connection == 0 || peer.session_id == 0 || peer.session_id > ARRAY_SIZE(g_connections))
        return nullptr;
    const size_t index = peer.session_id - 1;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    struct bt_conn* connection = nullptr;
    if (g_sessions.connection(peer.session_id) == peer.connection &&
        g_connections[index] != nullptr)
        connection = bt_conn_ref(g_connections[index]);
    k_spin_unlock(&g_lock, key);
    return connection;
}

uint16_t payload_from_mtu(uint16_t mtu) {
    return mtu > kNotifyHeaderBytes ? static_cast<uint16_t>(mtu - kNotifyHeaderBytes)
                                    : static_cast<uint16_t>(0);
}

// INFO: fc 19nov26 called under g_lock, after the sessions raised an Up or a Down
void forward_events() {
    LinkEvent event{};
    while (g_bus != nullptr && g_sessions.pop(event)) (void)g_bus->link_events.push(event);
}

void note_mtu_request(struct bt_conn* connection, MtuRequest outcome, uint8_t error) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_sessions.mtu_requested(session_of(connection), outcome, error);
    k_spin_unlock(&g_lock, key);
}

void mtu_exchanged(struct bt_conn* connection, uint8_t error,
                   struct bt_gatt_exchange_params* /*parameters*/) {
    if (g_connections[bt_conn_index(connection)] != connection) return;
    note_mtu_request(connection, error == 0 ? MtuRequest::Exchanged : MtuRequest::Refused, error);
}

struct bt_gatt_exchange_params g_mtu_exchange = {.func = mtu_exchanged};

void request_mtu(struct bt_conn* connection) {
    note_mtu_request(connection, MtuRequest::Pending, 0);
    const int error = bt_gatt_exchange_mtu(connection, &g_mtu_exchange);
    if (error == -EALREADY)
        note_mtu_request(connection, MtuRequest::AlreadyExchanged, 0);
    else if (error != 0)
        note_mtu_request(connection, MtuRequest::Unsent, static_cast<uint8_t>(-error));
}

// INFO: le 04aug26 the callbacks touch no service, they push onto the bus and wake the loop
// INFO: fc 25sep26 Zephyr drops a gone link's sent callbacks, so a new link starts at zero
// INFO: fc 11nov26 a phone past the ceiling a paired Bravo leaves is disconnected, never served
void connected(struct bt_conn* connection, uint8_t error) {
    if (error || !phone(connection)) return;
    const uint8_t index = bt_conn_index(connection);
    atomic_set(&g_in_flight[index], 0);
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const bool admitted =
        g_sessions.connected(session_of(connection), payload_from_mtu(bt_gatt_get_mtu(connection)),
                             address_of(bt_conn_get_dst(connection)));
    if (admitted) g_connections[index] = bt_conn_ref(connection);
    forward_events();
    k_spin_unlock(&g_lock, key);
    g_loop_wake.wake();
    if (!admitted) {
        (void)bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_LOW_RESOURCES);
        return;
    }
    request_mtu(connection);
    resume_advertising();
}
void disconnected(struct bt_conn* connection, uint8_t reason) {
    const uint8_t index = bt_conn_index(connection);
    if (g_connections[index] != connection) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_connections[index] = nullptr;
    g_sessions.disconnected(session_of(connection), reason);
    forward_events();
    k_spin_unlock(&g_lock, key);
    g_loop_wake.wake();
    bt_conn_unref(connection);
}
// INFO: fc 18sep26 Advertising stops on connect, and only recycled() has a free connection object.
void recycled() { resume_advertising(); }
BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected, .disconnected = disconnected, .recycled = recycled};

// INFO: fc 20nov26 the payload is learnt here, when an MTU exchange lands after connect, and tx is
// what our notifications may carry
void mtu_updated(struct bt_conn* connection, uint16_t tx, uint16_t /*rx*/) {
    if (g_connections[bt_conn_index(connection)] != connection) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_sessions.payload_changed(session_of(connection), payload_from_mtu(tx));
    forward_events();
    k_spin_unlock(&g_lock, key);
    g_loop_wake.wake();
}
// INFO: fc 20nov26 registered at runtime, because bt_gatt_cb is a runtime list, unlike the
// connection callbacks
struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

}  // namespace

// INFO: fc 18oct26 enabled here, advertised once the product names the unit (advertise_as)
Status Link::begin() {
    if (bt_enable(nullptr) != 0) return Status::Down;
    bt_gatt_cb_register(&gatt_callbacks);
    return Status::Ok;
}

Status Link::reserve_peer_connection(bool reserved) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status status = g_sessions.reserve_peer(reserved);
    const bool admits = g_sessions.admits();
    k_spin_unlock(&g_lock, key);
    return advertise_within_ceiling(status, admits);
}

Status Link::reserve_peer_connection_for(const events::BluetoothAddress& central) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status status = g_sessions.reserve_peer_for(central);
    const bool admits = g_sessions.admits();
    k_spin_unlock(&g_lock, key);
    return advertise_within_ceiling(status, admits);
}

bool Link::central_of(const events::Peer& peer, events::BluetoothAddress& out) const {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const bool live = g_sessions.central(peer, out);
    k_spin_unlock(&g_lock, key);
    return live;
}

int Link::phones() const {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const int phones = g_sessions.phones();
    k_spin_unlock(&g_lock, key);
    return phones;
}

int Link::centrals() const {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const int centrals = g_sessions.count();
    k_spin_unlock(&g_lock, key);
    return centrals;
}

uint16_t Link::payload_bytes() const {
    uint16_t smallest = 0;
    for (size_t i = 0; i < ARRAY_SIZE(g_connections); i++) {
        struct bt_conn* connection = connection_reference(i);
        if (connection == nullptr) continue;
        const uint16_t payload = payload_from_mtu(bt_gatt_get_mtu(connection));
        bt_conn_unref(connection);
        if (smallest == 0 || payload < smallest) smallest = payload;
    }
    return smallest < ports::kMinimumLinkPayloadBytes ? ports::kMinimumLinkPayloadBytes : smallest;
}

uint16_t Link::payload_bytes_to(uint16_t session_id) const {
    struct bt_conn* connection = connection_of(session_id);
    if (connection == nullptr) return ports::kMinimumLinkPayloadBytes;
    const uint16_t payload = payload_from_mtu(bt_gatt_get_mtu(connection));
    bt_conn_unref(connection);
    return payload < ports::kMinimumLinkPayloadBytes ? ports::kMinimumLinkPayloadBytes : payload;
}

uint32_t Link::connection(uint16_t session_id) const {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const uint32_t live = g_sessions.connection(session_id);
    k_spin_unlock(&g_lock, key);
    return live;
}

Status Link::notify_one(struct bt_conn* connection, Endpoint endpoint, ConstByteSpan bytes) {
    const struct bt_gatt_attr* attributes[kMaxNotifyAttributes] = {};
    const int attribute_count = notify_attributes(endpoint, attributes);
    if (attribute_count == 0) return Status::Invalid;
    atomic_t* in_flight = &g_in_flight[bt_conn_index(connection)];
    Status result = Status::WouldBlock;
    bool subscribed = false;
    for (int i = 0; i < attribute_count; i++) {
        if (!bt_gatt_is_subscribed(connection, attributes[i], BT_GATT_CCC_NOTIFY)) continue;
        subscribed = true;
        if (atomic_get(in_flight) >= kMaxNotifiesInFlight) return Status::WouldBlock;
        struct bt_gatt_notify_params notification = {};
        notification.attr = attributes[i];
        notification.data = bytes.data();
        notification.len = static_cast<uint16_t>(bytes.size());
        notification.func = notified;
        atomic_inc(in_flight);
        const int error = bt_gatt_notify_cb(connection, &notification);
        if (error == 0) return Status::Ok;
        atomic_dec(in_flight);
        if (error != -ENOMEM && error != -EAGAIN) result = Status::Invalid;
    }
    return subscribed ? result : Status::Unsubscribed;
}

Status Link::send(Endpoint endpoint, ConstByteSpan bytes) {
    if (bytes.size() > payload_bytes()) return Status::OutOfRange;
    Status result = Status::Down;
    bool delivered = false;
    for (size_t i = 0; i < ARRAY_SIZE(g_connections); i++) {
        struct bt_conn* connection = connection_reference(i);
        if (connection == nullptr) continue;
        const Status one = notify_one(connection, endpoint, bytes);
        bt_conn_unref(connection);
        if (one == Status::Ok)
            delivered = true;
        else if (one != Status::Unsubscribed || result == Status::Down)
            result = one;
    }
    return delivered ? Status::Ok : result;
}

Status Link::send_to(const events::Peer& peer, Endpoint endpoint, ConstByteSpan bytes) {
    if (bytes.size() > payload_bytes_to(peer.session_id)) return Status::OutOfRange;
    struct bt_conn* connection = connection_of(peer);
    if (connection == nullptr) return Status::Down;
    const Status sent = notify_one(connection, endpoint, bytes);
    bt_conn_unref(connection);
    // INFO: fc 26nov26 a central may write before it subscribes, so its answer waits as for buffers
    return sent == Status::Unsubscribed ? Status::WouldBlock : sent;
}

void Link::deliver_to(bus::Bus& bus) { g_bus = &bus; }

Link& link() {
    static Link instance;
    return instance;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
