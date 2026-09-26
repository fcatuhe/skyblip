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
#include "core/settings/address.h"
#include "core/util/fifo.h"
#include "core/util/format.h"

LOG_MODULE_DECLARE(skyblip, LOG_LEVEL_INF);

namespace skyblip::platform::zephyr {

using skyblip::events::Endpoint;
using skyblip::events::LinkEvent;
using skyblip::events::RxFrame;

namespace {

// INFO: fc 18sep26 Nordic's roles: the app writes RX, we notify TX (XCSoar BleSerialPort.java:100).
#define NUS_UUID(v) BT_UUID_128_ENCODE(0x6e400000 | (v), 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
static struct bt_uuid_128 nus_svc_uuid = BT_UUID_INIT_128(NUS_UUID(0x0001));
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0002));
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0003));

// INFO: fc 18sep26 HM-10, what PowerFLARM Flex ships and LK8000 only speaks: one characteristic.
static struct bt_uuid_16 hm10_svc_uuid = BT_UUID_INIT_16(0xFFE0);
static struct bt_uuid_16 hm10_chr_uuid = BT_UUID_INIT_16(0xFFE1);

// INFO: fc 18sep26 Our own base: config and log are our protocol, not a UART apps would misread.
#define SKB_UUID(v) BT_UUID_128_ENCODE(0x69c21300 | (v), 0x0187, 0x4204, 0xa91d, 0x02eb8858b440)
static struct bt_uuid_128 skb_svc_uuid = BT_UUID_INIT_128(SKB_UUID(0x01));
static struct bt_uuid_128 cfg_uuid = BT_UUID_INIT_128(SKB_UUID(0x02));
// The log offload is thousands of small round trips. On its own characteristic
// it cannot starve the one the pilot's prompts travel on, and an app that does
// not want logs simply never subscribes.
static struct bt_uuid_128 log_uuid = BT_UUID_INIT_128(SKB_UUID(0x03));

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
struct bt_conn* g_conns[CONFIG_BT_MAX_CONN] = {};
static_assert(CONFIG_BT_MAX_CONN <= static_cast<int>(comms::LinkSessions::kMaxSessions),
              "the controller admits more centrals than core will serve");

// INFO: fc 23sep26 a notify off the sysworkq waits forever for a buffer, so each link gets a share
constexpr atomic_val_t kNotifyInFlightMax = 4;
constexpr int kAttResponsesPerLink = 1;
// INFO: fc 25sep26 smp_bt.c waits on each notification's sent callback before the next
constexpr int kSmpNotifiesPerLink = 1;
static_assert(CONFIG_BT_ATT_TX_COUNT >=
                  (kNotifyInFlightMax + kAttResponsesPerLink + kSmpNotifiesPerLink) *
                      CONFIG_BT_MAX_CONN,
              "the ATT pool must hold every link's notifications, a response and an SMP reply");
atomic_t g_in_flight[CONFIG_BT_MAX_CONN];

void notified(struct bt_conn* conn, void* /*user_data*/) {
    atomic_dec(&g_in_flight[bt_conn_index(conn)]);
}

// INFO: fc 18sep26 bt_conn_index() is the controller's own slot, so session ids never collide.
uint16_t session_of(struct bt_conn* conn) { return static_cast<uint16_t>(bt_conn_index(conn) + 1); }

// INFO: fc 23sep26 referenced under the callbacks' lock, so a released connection is never notified
struct bt_conn* conn_ref(size_t index) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    struct bt_conn* conn = g_conns[index] != nullptr ? bt_conn_ref(g_conns[index]) : nullptr;
    k_spin_unlock(&g_lock, key);
    return conn;
}

struct bt_conn* conn_of(uint16_t session_id) {
    if (session_id == 0 || session_id > ARRAY_SIZE(g_conns)) return nullptr;
    return conn_ref(session_id - 1);
}

uint16_t payload_from_mtu(uint16_t mtu) {
    return mtu > kNotifyHeaderBytes ? static_cast<uint16_t>(mtu - kNotifyHeaderBytes)
                                    : static_cast<uint16_t>(0);
}

Status push_rx(struct bt_conn* conn, Endpoint endpoint, const void* buf, uint16_t len) {
    RxFrame f{};
    f.session_id = session_of(conn);
    f.endpoint = endpoint;
    f.len = len > f.data.size() ? static_cast<uint16_t>(f.data.size()) : len;
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    for (uint16_t i = 0; i < f.len; i++) f.data[i] = p[i];
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status queued = g_rx.push(f);
    k_spin_unlock(&g_lock, key);
    return queued;
}

// INFO: fc 04aug26 The inbound half of the same rule. With an ATT_MTU of 498 a
// central can write more than events::RxFrame carries, and a command cut to
// 256 bytes is not a command - a truncated "set" would apply the fields that
// survived. So it is refused with the ATT error that says exactly that, which the
// central sees, instead of being half-obeyed.
constexpr size_t kMaxInboundBytes = sizeof(RxFrame::data);
bool too_long(uint16_t len) { return len > kMaxInboundBytes; }

// INFO: fc 23sep26 a write with response learns the queue was full, one without is dropped unseen
ssize_t accept_write(struct bt_conn* conn, Endpoint endpoint, const void* buf, uint16_t len) {
    if (too_long(len)) return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    if (!is_ok(push_rx(conn, endpoint, buf, len)))
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    return len;
}

ssize_t on_cfg_write(struct bt_conn* conn, const struct bt_gatt_attr*, const void* buf,
                     uint16_t len, uint16_t /*offset*/, uint8_t /*flags*/) {
    return accept_write(conn, Endpoint::Config, buf, len);
}

ssize_t on_log_write(struct bt_conn* conn, const struct bt_gatt_attr*, const void* buf,
                     uint16_t len, uint16_t /*offset*/, uint8_t /*flags*/) {
    return accept_write(conn, Endpoint::Log, buf, len);
}

// INFO: fc 18sep26 XCSoar refuses a UART service with no write characteristic, nothing reads this.
ssize_t on_stream_write(struct bt_conn*, const struct bt_gatt_attr*, const void*, uint16_t len,
                        uint16_t /*offset*/, uint8_t /*flags*/) {
    return len;
}

BT_GATT_SERVICE_DEFINE(nus_svc, BT_GATT_PRIMARY_SERVICE(&nus_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, nullptr, on_stream_write,
                                              nullptr),
                       BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, nullptr, nullptr, nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kNusNotifyAttr = 4;

BT_GATT_SERVICE_DEFINE(hm10_svc, BT_GATT_PRIMARY_SERVICE(&hm10_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&hm10_chr_uuid.uuid,
                                              BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE |
                                                  BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, nullptr, on_stream_write,
                                              nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kHm10NotifyAttr = 2;

BT_GATT_SERVICE_DEFINE(skb_svc, BT_GATT_PRIMARY_SERVICE(&skb_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&cfg_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_cfg_write, nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&log_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP |
                                                  BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_WRITE, nullptr, on_log_write, nullptr),
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));
constexpr int kCfgNotifyAttr = 2;
constexpr int kLogNotifyAttr = 5;
constexpr int kMaxNotifyAttrs = 2;

int notify_attrs(Endpoint ep, const struct bt_gatt_attr** out) {
    switch (ep) {
        case Endpoint::Nmea:
            out[0] = &nus_svc.attrs[kNusNotifyAttr];
            out[1] = &hm10_svc.attrs[kHm10NotifyAttr];
            return 2;
        case Endpoint::Config: out[0] = &skb_svc.attrs[kCfgNotifyAttr]; return 1;
        case Endpoint::Log: out[0] = &skb_svc.attrs[kLogNotifyAttr]; return 1;
    }
    return 0;
}

// INFO: fc 18sep26 XCSoar scan-filters by service UUID, so a name-only advert is invisible to it.
const struct bt_data adv[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, NUS_UUID(0x0001)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0xFFE0)),
};

// INFO: fc 18sep26 Both UUIDs fill the 31-byte advert, so the name rides the scan response.
char g_name[CONFIG_BT_DEVICE_NAME_MAX] = CONFIG_BT_DEVICE_NAME;
struct bt_data sd[] = {BT_DATA(BT_DATA_NAME_COMPLETE, g_name, sizeof(CONFIG_BT_DEVICE_NAME) - 1)};

int start_advertising() {
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv, ARRAY_SIZE(adv), sd, ARRAY_SIZE(sd));
}

// INFO: fc 18sep26 -ENOMEM is every connection object in use, and recycled() is what undoes it.
void resume_advertising() {
    const int rc = start_advertising();
    if (rc != 0 && rc != -ENOMEM && rc != -EALREADY)
        LOG_ERR("bluetooth: advertising did not resume (%d), the device is invisible", rc);
}

// INFO: le 04aug26 The three callbacks are the whole producer side, and none of
// them touches a service: they hand the connection to comms::LinkSession under
// the spinlock exactly as a config write is handed to g_rx, and the board drains
// both onto the bus from the service loop. A Bluetooth callback runs on the host
// stack's own thread, so calling into a service from here would be a second path
// into core/ with no critical section around it.
// INFO: fc 25sep26 Zephyr drops a gone link's sent callbacks, so a new link starts at zero
void connected(struct bt_conn* conn, uint8_t err) {
    if (err) return;
    const uint8_t index = bt_conn_index(conn);
    struct bt_conn* held = bt_conn_ref(conn);
    atomic_set(&g_in_flight[index], 0);
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_conns[index] = held;
    g_sessions.connected(session_of(conn), payload_from_mtu(bt_gatt_get_mtu(conn)));
    k_spin_unlock(&g_lock, key);
    resume_advertising();
}
void disconnected(struct bt_conn* conn, uint8_t /*reason*/) {
    const uint8_t index = bt_conn_index(conn);
    if (g_conns[index] != conn) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_conns[index] = nullptr;
    g_sessions.disconnected(session_of(conn));
    k_spin_unlock(&g_lock, key);
    bt_conn_unref(conn);
}
// INFO: fc 18sep26 Advertising stops on connect, and only recycled() has a free connection object.
void recycled() { resume_advertising(); }
BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = connected, .disconnected = disconnected, .recycled = recycled};

// The central's ATT_EXCHANGE_MTU_REQ, landing after the connection is up: this is
// where the payload figure is actually learnt, so it is where the refreshed
// figure goes onto the bus. tx is our side of the pair - what a notification we
// send may carry.
void mtu_updated(struct bt_conn* conn, uint16_t tx, uint16_t /*rx*/) {
    if (g_conns[bt_conn_index(conn)] != conn) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_sessions.payload_changed(session_of(conn), payload_from_mtu(tx));
    k_spin_unlock(&g_lock, key);
}
// Registered rather than section-defined, unlike the connection callbacks above:
// bt_gatt_cb is a runtime list, and this is the call every Zephyr release offers
// for it.
struct bt_gatt_cb gatt_cbs = {.att_mtu_updated = mtu_updated};

}  // namespace

Status Link::begin(uint32_t device_addr) {
    if (bt_enable(nullptr) != 0) return Status::Down;
    bt_gatt_cb_register(&gatt_cbs);
    name_after(device_addr);
    if (start_advertising() != 0) return Status::Down;
    return Status::Ok;
}

// INFO: fc 18sep26 The address the panel shows, so a phone's list says what the glass says.
void Link::name_after(uint32_t device_addr) {
    const uint32_t shown = settings::air_address(device_addr);
    int n = fmt_string(g_name, CONFIG_BT_DEVICE_NAME);
    n += fmt_string(g_name + n, " ");
    n += fmt_hex(g_name + n, shown, 6);
    g_name[n] = 0;
    sd[0].data_len = static_cast<uint8_t>(n);
    bt_set_name(g_name);
}

// INFO: fc 04aug26 Nothing here asks for an MTU exchange. ATT_EXCHANGE_MTU_REQ
// is a client operation and this build is CONFIG_BT_PERIPHERAL with no GATT
// client, so bt_gatt_exchange_mtu() is not even compiled in; the spec allows one
// exchange per direction per connection, and every central we serve (iOS,
// Android, Chrome's Web Bluetooth) initiates it itself on connect. Waiting is
// correct as long as nothing assumes the result, which payload_bytes() is what
// stops.
uint16_t Link::payload_bytes() const {
    uint16_t smallest = 0;
    for (size_t i = 0; i < ARRAY_SIZE(g_conns); i++) {
        struct bt_conn* conn = conn_ref(i);
        if (conn == nullptr) continue;
        const uint16_t payload = payload_from_mtu(bt_gatt_get_mtu(conn));
        bt_conn_unref(conn);
        if (smallest == 0 || payload < smallest) smallest = payload;
    }
    return smallest < ports::kMinimumLinkPayload ? ports::kMinimumLinkPayload : smallest;
}

uint16_t Link::payload_bytes_to(uint16_t session_id) const {
    struct bt_conn* conn = conn_of(session_id);
    if (conn == nullptr) return ports::kMinimumLinkPayload;
    const uint16_t payload = payload_from_mtu(bt_gatt_get_mtu(conn));
    bt_conn_unref(conn);
    return payload < ports::kMinimumLinkPayload ? ports::kMinimumLinkPayload : payload;
}

Status Link::notify_one(struct bt_conn* conn, Endpoint ep, ConstByteSpan bytes) {
    const struct bt_gatt_attr* attrs[kMaxNotifyAttrs] = {};
    const int n = notify_attrs(ep, attrs);
    atomic_t* in_flight = &g_in_flight[bt_conn_index(conn)];
    Status result = Status::WouldBlock;
    bool subscribed = false;
    for (int i = 0; i < n; i++) {
        if (!bt_gatt_is_subscribed(conn, attrs[i], BT_GATT_CCC_NOTIFY)) continue;
        subscribed = true;
        if (atomic_get(in_flight) >= kNotifyInFlightMax) return Status::WouldBlock;
        struct bt_gatt_notify_params params = {};
        params.attr = attrs[i];
        params.data = bytes.data();
        params.len = static_cast<uint16_t>(bytes.size());
        params.func = notified;
        atomic_inc(in_flight);
        const int rc = bt_gatt_notify_cb(conn, &params);
        if (rc == 0) return Status::Ok;
        atomic_dec(in_flight);
        if (rc != -ENOMEM && rc != -EAGAIN) result = Status::Invalid;
    }
    return subscribed ? result : Status::WouldBlock;
}

Status Link::send(Endpoint ep, ConstByteSpan bytes) {
    if (bytes.size() > payload_bytes()) return Status::OutOfRange;
    Status result = Status::Down;
    bool delivered = false;
    for (size_t i = 0; i < ARRAY_SIZE(g_conns); i++) {
        struct bt_conn* conn = conn_ref(i);
        if (conn == nullptr) continue;
        const Status one = notify_one(conn, ep, bytes);
        bt_conn_unref(conn);
        if (one == Status::Ok)
            delivered = true;
        else
            result = one;
    }
    return delivered ? Status::Ok : result;
}

Status Link::send_to(uint16_t session_id, Endpoint ep, ConstByteSpan bytes) {
    if (bytes.size() > payload_bytes_to(session_id)) return Status::OutOfRange;
    struct bt_conn* conn = conn_of(session_id);
    if (conn == nullptr) return Status::Down;
    const Status sent = notify_one(conn, ep, bytes);
    bt_conn_unref(conn);
    return sent;
}

bool Link::pop_rx(RxFrame& out) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    Result<RxFrame> r = g_rx.pop();
    k_spin_unlock(&g_lock, key);
    if (!r.ok()) return false;
    out = r.value();
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
