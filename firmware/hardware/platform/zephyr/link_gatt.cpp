#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "core/events/link.h"
#include "hardware/platform/zephyr/link_state.h"
#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/skyblip_uuid.h"

namespace skyblip::platform::zephyr::link_state {

using skyblip::events::Endpoint;
using skyblip::events::RxFrame;

namespace {

static struct bt_uuid_128 nus_service_uuid = BT_UUID_INIT_128(NUS_UUID(0x0001));
static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0002));
static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(NUS_UUID(0x0003));

// INFO: fc 13nov26 HM-10, what PowerFLARM Fusion and Flex ship: one characteristic.
static struct bt_uuid_16 hm10_service_uuid = BT_UUID_INIT_16(0xFFE0);
static struct bt_uuid_16 hm10_characteristic_uuid = BT_UUID_INIT_16(0xFFE1);

static struct bt_uuid_128 skyblip_service_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(kSkyblipServiceId));
static struct bt_uuid_128 config_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(kConfigCharacteristicId));
// INFO: fc 20nov26 the log offload gets its own characteristic so its round trips cannot starve the
// pilot's prompts
static struct bt_uuid_128 log_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(kLogCharacteristicId));

Status push_rx(struct bt_conn* connection, Endpoint endpoint, const void* data, uint16_t bytes) {
    RxFrame frame{};
    frame.session_id = session_of(connection);
    frame.endpoint = endpoint;
    frame.bytes = bytes > frame.data.size() ? static_cast<uint16_t>(frame.data.size()) : bytes;
    const uint8_t* incoming = static_cast<const uint8_t*>(data);
    for (uint16_t i = 0; i < frame.bytes; i++) frame.data[i] = incoming[i];
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    frame.connection = g_sessions.connection(frame.session_id);
    const bool queued = g_bus != nullptr && g_bus->deliver(frame);
    k_spin_unlock(&g_lock, key);
    g_loop_wake.wake();
    return queued ? Status::Ok : Status::Full;
}

// INFO: fc 04aug26 a write longer than RxFrame is refused with an ATT error, because a truncated
// set would half-apply
constexpr size_t kMaxInboundBytes = sizeof(RxFrame::data);
bool too_long(uint16_t bytes) { return bytes > kMaxInboundBytes; }

// INFO: fc 23sep26 a write with response learns the queue was full, one without is dropped unseen
ssize_t accept_write(struct bt_conn* connection, Endpoint endpoint, const void* data,
                     uint16_t bytes) {
    if (!phone(connection)) return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
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

// INFO: fc 11nov26 Bravo serves peer to its Go, and the Go reaches Bravo's as a central
#if defined(CONFIG_SKYBLIP_PEER_SERVER)
static struct bt_uuid_128 peer_uuid = BT_UUID_INIT_128(SKYBLIP_UUID(kPeerCharacteristicId));

ssize_t on_peer_write(struct bt_conn* connection, const struct bt_gatt_attr*, const void* data,
                      uint16_t bytes, uint16_t /*offset*/, uint8_t /*flags*/) {
    return accept_write(connection, Endpoint::Peer, data, bytes);
}

#define SKYBLIP_PEER_ATTRIBUTES                                                         \
    ,                                                                                   \
        BT_GATT_CHARACTERISTIC(                                                         \
            &peer_uuid.uuid,                                                            \
            BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY, \
            BT_GATT_PERM_WRITE, nullptr, on_peer_write, nullptr),                       \
        BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
constexpr int kPeerNotifyAttribute = 8;
#else
#define SKYBLIP_PEER_ATTRIBUTES
#endif

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
                       BT_GATT_CCC(nullptr, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
                           SKYBLIP_PEER_ATTRIBUTES);
constexpr int kConfigNotifyAttribute = 2;
constexpr int kLogNotifyAttribute = 5;

}  // namespace

int notify_attributes(Endpoint endpoint, const struct bt_gatt_attr** out) {
    switch (endpoint) {
        case Endpoint::Nmea:
            out[0] = &nus_service.attrs[kNusNotifyAttribute];
            out[1] = &hm10_service.attrs[kHm10NotifyAttribute];
            return 2;
        case Endpoint::Config: out[0] = &skyblip_service.attrs[kConfigNotifyAttribute]; return 1;
        case Endpoint::Log: out[0] = &skyblip_service.attrs[kLogNotifyAttribute]; return 1;
        case Endpoint::Peer:
#if defined(CONFIG_SKYBLIP_PEER_SERVER)
            out[0] = &skyblip_service.attrs[kPeerNotifyAttribute];
            return 1;
#else
            return 0;
#endif
    }
    return 0;
}

}  // namespace skyblip::platform::zephyr::link_state
#endif  // __ZEPHYR__
