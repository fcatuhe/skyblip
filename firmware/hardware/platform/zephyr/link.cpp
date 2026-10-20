#if defined(__ZEPHYR__)

#include "hardware/platform/zephyr/link.h"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "core/comms/link_sessions.h"
#include "core/events/link.h"
#include "core/identity/activation.h"
#include "core/util/fifo.h"

LOG_MODULE_DECLARE(skyblip, LOG_LEVEL_INF);

namespace skyblip::platform::zephyr {

using skyblip::events::Endpoint;
using skyblip::events::LinkEvent;
using skyblip::events::MtuRequest;
using skyblip::events::RxFrame;

namespace {

// INFO: fc 18sep26 Nordic's roles: the app writes RX, we notify TX (XCSoar BleSerialPort.java:100).
#define NUS_UUID(id) BT_UUID_128_ENCODE(0x6e400000 | (id), 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
static struct bt_uuid_128 nus_service_uuid = BT_UUID_INIT_128(NUS_UUID(0x0001));
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0002));
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0003));

// INFO: fc 18sep26 HM-10, what PowerFLARM Flex ships and LK8000 only speaks: one characteristic.
static struct bt_uuid_16 hm10_service_uuid = BT_UUID_INIT_16(0xFFE0);
static struct bt_uuid_16 hm10_characteristic_uuid = BT_UUID_INIT_16(0xFFE1);

// INFO: fc 18sep26 Our own base: config and log are our protocol, not a UART apps would misread.
#define SKYBLIP_UUID(id) \
    BT_UUID_128_ENCODE(0x69c21300 | (id), 0x0187, 0x4204, 0xa91d, 0x02eb8858b440)
static struct bt_uuid_128 skyblip_service_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(0x01));
static struct bt_uuid_128 config_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(0x02));
// The log offload is thousands of small round trips. On its own characteristic
// it cannot starve the one the pilot's prompts travel on, and an app that does
// not want logs simply never subscribes.
static struct bt_uuid_128 log_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(0x03));

// INFO: fc 04aug26 The ATT notification header - opcode plus value handle. What
// is left of the ATT_MTU is what one notification may carry, which is why 185
// (an iPhone's usual answer) is 182 bytes of payload and not 185.
constexpr uint16_t kNotifyHeaderBytes = 3;

Fifo<RxFrame, 8> g_rx;
// The same lock the inbound frames use, because it guards the same handover: a
// Bluetooth callback writes, the service loop reads, and a lifecycle event
// interleaved with a frame must not tear either of them.
struct k_spinlock g_lock;
comms::LinkSessions g_sessions;
struct bt_conn* g_connections[CONFIG_BT_MAX_CONN] = {};
static_assert(CONFIG_BT_MAX_CONN <= static_cast<int>(comms::LinkSessions::kMaxSessions),
              "the controller admits more centrals than core will serve");

// INFO: fc 23sep26 a notify off the sysworkq waits forever for a buffer, so each link gets a share
constexpr atomic_val_t kMaxNotifiesInFlight = 4;
constexpr int kAttResponsesPerLink = 1;
// INFO: fc 25sep26 smp_bt.c waits on each notification's sent callback before the next
constexpr int kSmpNotifiesPerLink = 1;
static_assert(CONFIG_BT_ATT_TX_COUNT >=
                  (kMaxNotifiesInFlight + kAttResponsesPerLink + kSmpNotifiesPerLink) *
                      CONFIG_BT_MAX_CONN,
              "the ATT pool must hold every link's notifications, a response and an SMP reply");
atomic_t g_in_flight[CONFIG_BT_MAX_CONN];

void notified(struct bt_conn* connection, void* /*user_data*/) {
    atomic_dec(&g_in_flight[bt_conn_index(connection)]);
}

// INFO: fc 18sep26 bt_conn_index() is the controller's own slot, so session ids never collide.
uint16_t session_of(struct bt_conn* connection) {
    return static_cast<uint16_t>(bt_conn_index(connection) + 1);
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

uint16_t payload_from_mtu(uint16_t mtu) {
    return mtu > kNotifyHeaderBytes ? static_cast<uint16_t>(mtu - kNotifyHeaderBytes)
                                    : static_cast<uint16_t>(0);
}

Status push_rx(struct bt_conn* connection, Endpoint endpoint, const void* data, uint16_t bytes) {
    RxFrame frame{};
    frame.session_id = session_of(connection);
    frame.endpoint = endpoint;
    frame.bytes = bytes > frame.data.size() ? static_cast<uint16_t>(frame.data.size()) : bytes;
    const uint8_t* incoming = static_cast<const uint8_t*>(data);
    for (uint16_t i = 0; i < frame.bytes; i++) frame.data[i] = incoming[i];
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status queued = g_rx.push(frame);
    k_spin_unlock(&g_lock, key);
    return queued;
}

// INFO: fc 04aug26 The inbound half of the same rule. With an ATT_MTU of 498 a
// central can write more than events::RxFrame carries, and a command cut to
// 256 bytes is not a command - a truncated "set" would apply the fields that
// survived. So it is refused with the ATT error that says exactly that, which the
// central sees, instead of being half-obeyed.
constexpr size_t kMaxInboundBytes = sizeof(RxFrame::data);
bool too_long(uint16_t bytes) { return bytes > kMaxInboundBytes; }

// INFO: fc 23sep26 a write with response learns the queue was full, one without is dropped unseen
ssize_t accept_write(struct bt_conn* connection, Endpoint endpoint, const void* data,
                     uint16_t bytes) {
    if (too_long(bytes)) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    if (!is_ok(push_rx(connection, endpoint, data, bytes)))
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    return bytes;
}

ssize_t on_config_write(struct bt_conn* connection, const struct bt_gatt_attr*, const void* data,
                        uint16_t bytes, uint16_t /*offset*/, uint8_t /*flags*/) {
    return accept_write(connection, Endpoint::Config, data, bytes);
}

ssize_t on_log_write(struct bt_conn* connection, const struct bt_gatt_attr*, const void* data,
                     uint16_t bytes, uint16_t /*offset*/, uint8_t /*flags*/) {
    return accept_write(connection, Endpoint::Log, data, bytes);
}

// INFO: fc 18sep26 XCSoar refuses a UART service with no write characteristic, nothing reads this.
ssize_t on_stream_write(struct bt_conn*, const struct bt_gatt_attr*, const void*, uint16_t bytes,
                        uint16_t /*offset*/, uint8_t /*flags*/) {
    return bytes;
}

BT_GATT_SERVICE_DEFINE(nus_service, BT_GATT_PRIMARY_SERVICE(&nus_service_uuid),
                       BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, nullptr, on_stream_write,
                                              nullptr),
                       BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kNusNotifyAttribute = 4;

BT_GATT_SERVICE_DEFINE(hm10_service, BT_GATT_PRIMARY_SERVICE(&hm10_service_uuid),
                       BT_GATT_CHARACTERISTIC(&hm10_characteristic_uuid.uuid,
                                              BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE |
                                                  BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, nullptr, on_stream_write,
                                              nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kHm10NotifyAttribute = 2;

BT_GATT_SERVICE_DEFINE(skyblip_service, BT_GATT_PRIMARY_SERVICE(&skyblip_service_uuid),
                       BT_GATT_CHARACTERISTIC(&config_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_config_write,
                                              nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&log_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_log_write, nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kConfigNotifyAttribute = 2;
constexpr int kLogNotifyAttribute = 5;
constexpr int kMaxNotifyAttributes = 2;

int notify_attributes(Endpoint endpoint, const struct bt_gatt_attr** out) {
    switch (endpoint) {
        case Endpoint::Nmea:
            out[0] = &nus_service.attrs[kNusNotifyAttribute];
            out[1] = &hm10_service.attrs[kHm10NotifyAttribute];
            return 2;
        case Endpoint::Config: out[0] = &skyblip_service.attrs[kConfigNotifyAttribute]; return 1;
        case Endpoint::Log: out[0] = &skyblip_service.attrs[kLogNotifyAttribute]; return 1;
    }
    return 0;
}

// INFO: fc 18sep26 XCSoar scan-filters by service UUID, so a name-only advert is invisible to it.
const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, NUS_UUID(0x0001)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0xFFE0)),
};

// INFO: fc 18sep26 Both UUIDs fill the 31-byte advert, so the name rides the scan response.
static_assert(identity::kBluetoothNameBytes < CONFIG_BT_DEVICE_NAME_MAX,
              "every name the product builds fits the GAP name and its terminator");
struct ScanResponse {
    char name[CONFIG_BT_DEVICE_NAME_MAX];
    struct bt_data scan_data[1];
};
// INFO: fc 18oct26 two, swapped whole: the loop renames while the host may resume
ScanResponse g_responses[2];
atomic_ptr_t g_response = ATOMIC_PTR_INIT(nullptr);
atomic_t g_advertising = ATOMIC_INIT(0);

int start_advertising() {
    const ScanResponse* response = static_cast<const ScanResponse*>(atomic_ptr_get(&g_response));
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising_data, ARRAY_SIZE(advertising_data),
                           response->scan_data, ARRAY_SIZE(response->scan_data));
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

// INFO: fc 18sep26 -ENOMEM is every connection object in use, and recycled() is what undoes it.
void resume_advertising() {
    if (atomic_get(&g_advertising) == 0) return;
    const int error = start_advertising();
    if (error != 0 && error != -ENOMEM && error != -EALREADY)
        LOG_ERR("bluetooth: advertising did not resume (%d), the device is invisible", error);
}

// INFO: le 04aug26 The three callbacks are the whole producer side, and none of
// them touches a service: they hand the connection to comms::LinkSession under
// the spinlock exactly as a config write is handed to g_rx, and the board drains
// both onto the bus from the service loop. A Bluetooth callback runs on the host
// stack's own thread, so calling into a service from here would be a second path
// into core/ with no critical section around it.
// INFO: fc 25sep26 Zephyr drops a gone link's sent callbacks, so a new link starts at zero
void connected(struct bt_conn* connection, uint8_t error) {
    if (error) return;
    const uint8_t index = bt_conn_index(connection);
    struct bt_conn* held = bt_conn_ref(connection);
    atomic_set(&g_in_flight[index], 0);
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_connections[index] = held;
    g_sessions.connected(session_of(connection), payload_from_mtu(bt_gatt_get_mtu(connection)));
    k_spin_unlock(&g_lock, key);
    request_mtu(connection);
    resume_advertising();
}
void disconnected(struct bt_conn* connection, uint8_t /*reason*/) {
    const uint8_t index = bt_conn_index(connection);
    if (g_connections[index] != connection) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_connections[index] = nullptr;
    g_sessions.disconnected(session_of(connection));
    k_spin_unlock(&g_lock, key);
    bt_conn_unref(connection);
}
// INFO: fc 18sep26 Advertising stops on connect, and only recycled() has a free connection object.
void recycled() { resume_advertising(); }
BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected, .disconnected = disconnected, .recycled = recycled};

// An ATT_EXCHANGE_MTU_REQ answered, the central's or the unit's own, landing after
// the connection is up: this is where the payload figure is actually learnt, so it
// is where the refreshed figure goes onto the bus. tx is our side of the pair -
// what a notification we send may carry.
void mtu_updated(struct bt_conn* connection, uint16_t tx, uint16_t /*rx*/) {
    if (g_connections[bt_conn_index(connection)] != connection) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_sessions.payload_changed(session_of(connection), payload_from_mtu(tx));
    k_spin_unlock(&g_lock, key);
}
// Registered rather than section-defined, unlike the connection callbacks above:
// bt_gatt_cb is a runtime list, and this is the call every Zephyr release offers
// for it.
struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

}  // namespace

// INFO: fc 18oct26 enabled here, advertised once the product names the unit (advertise_as)
Status Link::begin() {
    if (bt_enable(nullptr) != 0) return Status::Down;
    bt_gatt_cb_register(&gatt_callbacks);
    return Status::Ok;
}

// INFO: fc 18oct26 a live rename: a link connected now sees the new name at its next scan
Status Link::advertise_as(const char* name) {
    ScanResponse* next =
        atomic_ptr_get(&g_response) == &g_responses[0] ? &g_responses[1] : &g_responses[0];
    size_t name_bytes = 0;
    while (name[name_bytes] != 0 && name_bytes < sizeof(next->name) - 1) {
        next->name[name_bytes] = name[name_bytes];
        name_bytes++;
    }
    next->name[name_bytes] = 0;
    next->scan_data[0].type = BT_DATA_NAME_COMPLETE;
    next->scan_data[0].data_len = static_cast<uint8_t>(name_bytes);
    next->scan_data[0].data = reinterpret_cast<const uint8_t*>(next->name);
    atomic_ptr_set(&g_response, next);

    const int named = bt_set_name(next->name);
    if (named != 0) LOG_ERR("bluetooth: the GAP name was not set (%d)", named);
    int error = 0;
    if (atomic_get(&g_advertising) == 0) {
        error = start_advertising();
        if (error == 0) atomic_set(&g_advertising, 1);
    } else {
        error = bt_le_adv_update_data(advertising_data, ARRAY_SIZE(advertising_data),
                                      next->scan_data, ARRAY_SIZE(next->scan_data));
        if (error == -EAGAIN) error = 0;
    }
    if (error != 0) LOG_ERR("bluetooth: advertising as %s failed (%d)", next->name, error);
    return named == 0 && error == 0 ? Status::Ok : Status::Down;
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

Status Link::notify_one(struct bt_conn* connection, Endpoint endpoint, ConstByteSpan bytes) {
    const struct bt_gatt_attr* attributes[kMaxNotifyAttributes] = {};
    const int attribute_count = notify_attributes(endpoint, attributes);
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
    return subscribed ? result : Status::WouldBlock;
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
        else
            result = one;
    }
    return delivered ? Status::Ok : result;
}

Status Link::send_to(uint16_t session_id, Endpoint endpoint, ConstByteSpan bytes) {
    if (bytes.size() > payload_bytes_to(session_id)) return Status::OutOfRange;
    struct bt_conn* connection = connection_of(session_id);
    if (connection == nullptr) return Status::Down;
    const Status sent = notify_one(connection, endpoint, bytes);
    bt_conn_unref(connection);
    return sent;
}

void Link::note_reply_too_wide(uint16_t session_id) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_sessions.reply_too_wide(session_id);
    k_spin_unlock(&g_lock, key);
}

bool Link::pop_rx(RxFrame& out) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    Result<RxFrame> popped = g_rx.pop();
    k_spin_unlock(&g_lock, key);
    if (!popped.ok()) return false;
    out = popped.value();
    return true;
}

bool Link::pop_event(LinkEvent& out) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const bool got = g_sessions.pop(out);
    k_spin_unlock(&g_lock, key);
    return got;
}

Link& link() {
    static Link instance;
    return instance;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
