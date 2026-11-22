#if defined(__ZEPHYR__)

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include "core/identity/activation.h"
#include "hardware/platform/zephyr/link.h"
#include "hardware/platform/zephyr/link_state.h"

namespace skyblip::platform::zephyr {

using link_state::g_lock;
using link_state::g_sessions;
using link_state::resume_advertising;

namespace {

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

}  // namespace

// INFO: fc 18sep26 -ENOMEM is every connection object in use, and recycled() is what undoes it.
void link_state::resume_advertising() {
    if (atomic_get(&g_advertising) == 0) return;
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    const bool admits = g_sessions.admits();
    k_spin_unlock(&g_lock, key);
    if (admits) (void)start_advertising();
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
    int error = 0;
    if (atomic_get(&g_advertising) == 0) {
        error = start_advertising();
        if (error == 0) atomic_set(&g_advertising, 1);
    } else {
        error = bt_le_adv_update_data(advertising_data, ARRAY_SIZE(advertising_data),
                                      next->scan_data, ARRAY_SIZE(next->scan_data));
        if (error == -EAGAIN) error = 0;
    }
    return named == 0 && error == 0 ? Status::Ok : Status::Down;
}

Status Link::advertise_within_ceiling(Status status, bool admits) {
    if (status != Status::Ok) return status;
    if (admits) {
        resume_advertising();
        return status;
    }
    (void)bt_le_adv_stop();
    return status;
}

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
