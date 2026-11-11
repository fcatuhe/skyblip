#if defined(__ZEPHYR__) && defined(CONFIG_BT_CENTRAL)

#include "hardware/platform/zephyr/central.h"

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cstring>

#include "core/util/fifo.h"
#include "hardware/platform/zephyr/att_budget.h"
#include "hardware/platform/zephyr/skyblip_uuid.h"

LOG_MODULE_DECLARE(skyblip, LOG_LEVEL_INF);

namespace skyblip::platform::zephyr {

using skyblip::events::CentralEvent;
using skyblip::events::CentralEventType;
using skyblip::events::RxFrame;

namespace {

static struct bt_uuid_128 peer_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(kPeerCharacteristicId));

constexpr uint16_t kNotifyHeaderBytes = 3;
// INFO: fc 11nov26 0.625 ms units, 0x0004 to 0x4000 (zephyr bluetooth/host/scan.c:1735, v4.4.2)
constexpr uint32_t kScanUnitUs = 625;
constexpr uint32_t kScanUnitsFloor = 0x0004;
constexpr uint32_t kScanUnitsCeiling = 0x4000;
// INFO: fc 11nov26 a loss is logged at most this often, with every count since boot
constexpr int64_t kLossReportPeriodMs = 10000;

struct k_spinlock g_lock;
Fifo<CentralEvent, 8> g_events;
Fifo<RxFrame, 8> g_rx;
atomic_t g_dropped = ATOMIC_INIT(0);
atomic_t g_up = ATOMIC_INIT(0);
// INFO: fc 11nov26 written and cleared under g_lock only, and one central link at most
struct bt_conn* g_peer = nullptr;
events::BluetoothAddress g_wanted{};
uint16_t g_value_handle = 0;
const char* g_name_prefix = "";
size_t g_name_prefix_bytes = 0;

// INFO: fc 11nov26 off the sysworkq att.c:794-815 waits forever for a buffer, so writes go there
struct PendingWrite {
    struct bt_conn* connection;
    uint16_t value_handle;
    uint16_t bytes;
    uint32_t generation;
    uint8_t data[sizeof(RxFrame::data)];
};
PendingWrite g_write = {};
atomic_t g_writes_in_flight = ATOMIC_INIT(0);
atomic_t g_write_refusals = ATOMIC_INIT(0);
atomic_t g_generation = ATOMIC_INIT(0);

void written(struct bt_conn* /*connection*/, void* user_data) {
    const auto generation = static_cast<atomic_val_t>(reinterpret_cast<uintptr_t>(user_data));
    if (generation == atomic_get(&g_generation)) atomic_dec(&g_writes_in_flight);
}

void send_write(struct k_work* /*work*/) {
    struct bt_conn* connection = g_write.connection;
    g_write.connection = nullptr;
    const int error = bt_gatt_write_without_response_cb(
        connection, g_write.value_handle, g_write.data, g_write.bytes, false, written,
        reinterpret_cast<void*>(static_cast<uintptr_t>(g_write.generation)));
    bt_conn_unref(connection);
    if (error == 0) return;
    atomic_inc(&g_write_refusals);
    atomic_dec(&g_writes_in_flight);
}

K_WORK_DEFINE(g_write_work, send_write);

struct bt_gatt_exchange_params g_exchange = {};
struct bt_gatt_discover_params g_discover = {};
struct bt_gatt_discover_params g_ccc_discover = {};
struct bt_gatt_subscribe_params g_subscribe = {};

uint16_t scan_units(uint32_t us) {
    const uint32_t units = us / kScanUnitUs;
    if (units < kScanUnitsFloor) return kScanUnitsFloor;
    return static_cast<uint16_t>(units > kScanUnitsCeiling ? kScanUnitsCeiling : units);
}

uint16_t payload_from_mtu(uint16_t mtu) {
    return mtu > kNotifyHeaderBytes ? static_cast<uint16_t>(mtu - kNotifyHeaderBytes)
                                    : static_cast<uint16_t>(0);
}

events::BluetoothAddress address_of(const bt_addr_le_t* address) {
    events::BluetoothAddress out{};
    out.type = address->type;
    for (size_t i = 0; i < out.bytes.size(); i++) out.bytes[i] = address->a.val[i];
    return out;
}

bt_addr_le_t le_address(const events::BluetoothAddress& address) {
    bt_addr_le_t out = {};
    out.type = address.type;
    for (size_t i = 0; i < address.bytes.size(); i++) out.a.val[i] = address.bytes[i];
    return out;
}

void raise(const CentralEvent& event) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status queued = g_events.push(event);
    k_spin_unlock(&g_lock, key);
    if (!is_ok(queued)) atomic_inc(&g_dropped);
}

void raise(CentralEventType type, uint16_t payload_bytes = 0) {
    CentralEvent event{};
    event.type = type;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    event.address = g_wanted;
    k_spin_unlock(&g_lock, key);
    event.payload_bytes = payload_bytes;
    raise(event);
}

bool as_central(struct bt_conn* connection) {
    struct bt_conn_info info = {};
    return bt_conn_get_info(connection, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

struct bt_conn* peer_reference() {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    struct bt_conn* connection = g_peer != nullptr ? bt_conn_ref(g_peer) : nullptr;
    k_spin_unlock(&g_lock, key);
    return connection;
}

// INFO: fc 11nov26 only the connection the lock still holds is released, so it is unreferenced once
void release_peer(struct bt_conn* connection) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    struct bt_conn* held = g_peer == connection ? g_peer : nullptr;
    if (held != nullptr) {
        g_peer = nullptr;
        atomic_set(&g_up, 0);
        g_value_handle = 0;
    }
    k_spin_unlock(&g_lock, key);
    if (held == nullptr) return;
    bt_conn_unref(held);
    raise(CentralEventType::Down);
}

Status refused(Status status) {
    atomic_inc(&g_write_refusals);
    return status;
}

void report_losses() {
    static atomic_val_t reported_drops = 0;
    static atomic_val_t reported_refusals = 0;
    static int64_t reported_ms = -kLossReportPeriodMs;
    const atomic_val_t drops = atomic_get(&g_dropped);
    const atomic_val_t refusals = atomic_get(&g_write_refusals);
    if (drops == reported_drops && refusals == reported_refusals) return;
    const int64_t now_ms = k_uptime_get();
    if (now_ms - reported_ms < kLossReportPeriodMs) return;
    reported_ms = now_ms;
    reported_drops = drops;
    reported_refusals = refusals;
    LOG_WRN(
        "bluetooth: Bravo's link dropped %ld events or frames and refused %ld writes since boot",
        static_cast<long>(drops), static_cast<long>(refusals));
}

void abandon(struct bt_conn* connection, const char* step, int error) {
    LOG_ERR("bluetooth: the link to Bravo failed at %s (%d)", step, error);
    const int dropped = bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (dropped != 0 && dropped != -ENOTCONN)
        LOG_ERR("bluetooth: the link to Bravo would not drop (%d)", dropped);
}

bool read_name(struct bt_data* data, void* user_data) {
    if (data->type != BT_DATA_NAME_COMPLETE) return true;
    char* name = static_cast<char*>(user_data);
    const size_t bytes = data->data_len < events::kAdvertisedNameBytes
                             ? data->data_len
                             : events::kAdvertisedNameBytes;
    std::memcpy(name, data->data, bytes);
    name[bytes] = 0;
    return false;
}

// INFO: fc 11nov26 the name rides the scan response, as link.cpp advertises it
void scanned(const bt_addr_le_t* address, int8_t /*rssi*/, uint8_t type,
             struct net_buf_simple* ad) {
    if (type != BT_GAP_ADV_TYPE_SCAN_RSP) return;
    CentralEvent event{};
    event.type = CentralEventType::Found;
    event.address = address_of(address);
    bt_data_parse(ad, read_name, event.name);
    if (std::strncmp(event.name, g_name_prefix, g_name_prefix_bytes) != 0) return;
    if (event.name[0] != 0) raise(event);
}

uint8_t notified(struct bt_conn* /*connection*/, struct bt_gatt_subscribe_params* /*params*/,
                 const void* data, uint16_t bytes) {
    if (data == nullptr) return BT_GATT_ITER_STOP;
    if (bytes > sizeof(RxFrame::data)) {
        atomic_inc(&g_dropped);
        return BT_GATT_ITER_CONTINUE;
    }
    RxFrame frame{};
    frame.endpoint = events::Endpoint::Peer;
    frame.bytes = bytes;
    std::memcpy(frame.data.data(), data, bytes);
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const Status queued = g_rx.push(frame);
    k_spin_unlock(&g_lock, key);
    if (!is_ok(queued)) atomic_inc(&g_dropped);
    return BT_GATT_ITER_CONTINUE;
}

void subscribed(struct bt_conn* connection, uint8_t error,
                struct bt_gatt_subscribe_params* /*params*/) {
    if (error != 0) {
        abandon(connection, "the subscription", error);
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_value_handle = g_subscribe.value_handle;
    atomic_set(&g_up, 1);
    k_spin_unlock(&g_lock, key);
    raise(CentralEventType::Up, payload_from_mtu(bt_gatt_get_mtu(connection)));
}

uint8_t discovered(struct bt_conn* connection, const struct bt_gatt_attr* attribute,
                   struct bt_gatt_discover_params* /*params*/) {
    if (attribute == nullptr) {
        abandon(connection, "the peer characteristic", -ENOENT);
        return BT_GATT_ITER_STOP;
    }
    const auto* characteristic = static_cast<const struct bt_gatt_chrc*>(attribute->user_data);
    g_subscribe = {};
    g_subscribe.notify = notified;
    g_subscribe.subscribe = subscribed;
    g_subscribe.value_handle = characteristic->value_handle;
    g_subscribe.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
    g_subscribe.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    g_subscribe.disc_params = &g_ccc_discover;
    g_subscribe.value = BT_GATT_CCC_NOTIFY;
    const int error = bt_gatt_subscribe(connection, &g_subscribe);
    if (error != 0 && error != -EALREADY) abandon(connection, "subscribing", error);
    return BT_GATT_ITER_STOP;
}

void discover(struct bt_conn* connection) {
    g_discover = {};
    g_discover.uuid = &peer_uuid.uuid;
    g_discover.func = discovered;
    g_discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    g_discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    g_discover.type = BT_GATT_DISCOVER_CHARACTERISTIC;
    const int error = bt_gatt_discover(connection, &g_discover);
    if (error != 0) abandon(connection, "discovery", error);
}

// INFO: fc 11nov26 a refused exchange leaves 20 bytes, which the Go refuses as too narrow at Up
void exchanged(struct bt_conn* connection, uint8_t /*error*/,
               struct bt_gatt_exchange_params* /*params*/) {
    discover(connection);
}

void connected(struct bt_conn* connection, uint8_t error) {
    if (!as_central(connection)) return;
    if (error != 0) {
        release_peer(connection);
        return;
    }
    // INFO: fc 11nov26 Zephyr drops a gone link's sent callbacks, so a new link counts from zero
    atomic_inc(&g_generation);
    if (!k_work_is_pending(&g_write_work)) atomic_set(&g_writes_in_flight, 0);
    g_exchange.func = exchanged;
    const int asked = bt_gatt_exchange_mtu(connection, &g_exchange);
    if (asked == -EALREADY)
        discover(connection);
    else if (asked != 0)
        abandon(connection, "the MTU exchange", asked);
}

void disconnected(struct bt_conn* connection, uint8_t /*reason*/) {
    if (!as_central(connection)) return;
    release_peer(connection);
}

BT_CONN_CB_DEFINE(central_callbacks) = {.connected = connected, .disconnected = disconnected};

}  // namespace

Status Central::scan(const ports::ScanDuty& duty, const char* name_prefix) {
    g_name_prefix = name_prefix;
    g_name_prefix_bytes = std::strlen(name_prefix);
    struct bt_le_scan_param parameters = {};
    parameters.type = BT_LE_SCAN_TYPE_ACTIVE;
    parameters.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE;
    parameters.interval = scan_units(duty.period_us);
    parameters.window = scan_units(duty.window_us);
    const int error = bt_le_scan_start(&parameters, scanned);
    if (error == 0 || error == -EALREADY) return Status::Ok;
    LOG_ERR("bluetooth: the scan for Bravo did not start (%d)", error);
    return Status::Down;
}

void Central::stop_scan() {
    const int error = bt_le_scan_stop();
    if (error != 0 && error != -EALREADY) LOG_ERR("bluetooth: the scan did not stop (%d)", error);
}

Status Central::connect(const events::BluetoothAddress& address, const ports::ScanDuty& duty) {
    struct bt_conn* held = peer_reference();
    if (held != nullptr) {
        bt_conn_unref(held);
        return Status::WouldBlock;
    }
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    g_wanted = address;
    k_spin_unlock(&g_lock, key);
    const bt_addr_le_t peer = le_address(address);
    const struct bt_conn_le_create_param create = BT_CONN_LE_CREATE_PARAM_INIT(
        BT_CONN_LE_OPT_NONE, scan_units(duty.period_us), scan_units(duty.window_us));
    struct bt_conn* created = nullptr;
    const int error = bt_conn_le_create(&peer, &create, BT_LE_CONN_PARAM_DEFAULT, &created);
    if (error == -EAGAIN || error == -EALREADY || error == -ENOMEM) return Status::WouldBlock;
    if (error != 0) {
        LOG_ERR("bluetooth: the connection to Bravo was not started (%d)", error);
        return Status::Down;
    }
    key = k_spin_lock(&g_lock);
    g_peer = created;
    struct bt_conn* probe = bt_conn_ref(created);
    k_spin_unlock(&g_lock, key);
    // INFO: fc 11nov26 a create that failed before it was published released nothing, so release it
    struct bt_conn_info info = {};
    if (bt_conn_get_info(probe, &info) == 0 && info.state == BT_CONN_STATE_DISCONNECTED)
        release_peer(probe);
    bt_conn_unref(probe);
    return Status::Ok;
}

void Central::disconnect() {
    struct bt_conn* connection = peer_reference();
    if (connection == nullptr) return;
    const int error = bt_conn_disconnect(connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (error != 0 && error != -ENOTCONN) LOG_ERR("bluetooth: Bravo would not drop (%d)", error);
    bt_conn_unref(connection);
}

// INFO: fc 11nov26 never blocks, and takes connection and handle under the lock that clears them
Status Central::write(ConstByteSpan bytes) {
    if (bytes.size() > sizeof(g_write.data)) return refused(Status::OutOfRange);
    if (atomic_get(&g_writes_in_flight) >= kCentralWritesInFlight)
        return refused(Status::WouldBlock);
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const bool up = atomic_get(&g_up) != 0 && g_peer != nullptr && g_value_handle != 0;
    struct bt_conn* connection = up ? bt_conn_ref(g_peer) : nullptr;
    const uint16_t value_handle = g_value_handle;
    k_spin_unlock(&g_lock, key);
    if (connection == nullptr) return refused(Status::Down);
    if (bytes.size() > payload_from_mtu(bt_gatt_get_mtu(connection))) {
        bt_conn_unref(connection);
        return refused(Status::OutOfRange);
    }
    atomic_inc(&g_writes_in_flight);
    g_write.connection = connection;
    g_write.value_handle = value_handle;
    g_write.bytes = static_cast<uint16_t>(bytes.size());
    g_write.generation = static_cast<uint32_t>(atomic_get(&g_generation));
    std::memcpy(g_write.data, bytes.data(), bytes.size());
    if (k_work_submit(&g_write_work) >= 0) return Status::Ok;
    g_write.connection = nullptr;
    bt_conn_unref(connection);
    atomic_dec(&g_writes_in_flight);
    return refused(Status::Down);
}

bool Central::pop_event(CentralEvent& out) {
    report_losses();
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    Result<CentralEvent> popped = g_events.pop();
    k_spin_unlock(&g_lock, key);
    if (!popped.ok()) return false;
    out = popped.value();
    return true;
}

bool Central::pop_rx(RxFrame& out) {
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    Result<RxFrame> popped = g_rx.pop();
    k_spin_unlock(&g_lock, key);
    if (!popped.ok()) return false;
    out = popped.value();
    return true;
}

uint32_t Central::dropped() const { return static_cast<uint32_t>(atomic_get(&g_dropped)); }

uint32_t Central::writes_refused() const {
    return static_cast<uint32_t>(atomic_get(&g_write_refusals));
}

Central& central() {
    static Central instance;
    return instance;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__ && CONFIG_BT_CENTRAL
