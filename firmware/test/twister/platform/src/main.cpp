// What the pure-host suite cannot reach: the Zephyr-backed adapters themselves.
// Runs under native_sim in CI and on the board when one is attached.
#include <zephyr/ztest.h>

#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"

using namespace skyblip;

ZTEST_SUITE(platform_adapters, NULL, NULL, NULL, NULL, NULL);

ZTEST(platform_adapters, test_kvstore_round_trips_through_nvs) {
    platform::zephyr::KeyValueStore key_value;
    zassert_equal(key_value.begin(), Status::Ok, "NVS did not come up");

    const uint8_t written[] = {1, 2, 3, 4};
    zassert_equal(key_value.write("settings", written, sizeof(written)), Status::Ok);

    uint8_t read[8] = {0};
    size_t stored_bytes = 0;
    zassert_equal(key_value.read("settings", read, sizeof(read), stored_bytes), Status::Ok);
    zassert_equal(stored_bytes, sizeof(written));
    zassert_mem_equal(read, written, sizeof(written));
}

// NVS never reads sector 2 of an otherwise clean store, so only the check can erase a ring page
// there.
ZTEST(platform_adapters, test_kvstore_erases_a_meshtastic_ring_before_mounting) {
    const struct device* flash = PARTITION_DEVICE(storage_partition);
    const off_t start = PARTITION_OFFSET(storage_partition);
    struct flash_pages_info page;
    zassert_equal(flash_get_page_info_by_offs(flash, start, &page), 0);
    zassert_equal(flash_flatten(flash, start, 3 * page.size), 0);
    const off_t ring_page = start + static_cast<off_t>(2 * page.size);
    const uint32_t header[2] = {platform::kWarmRingMagicV3, 7};
    zassert_equal(flash_write(flash, ring_page, header, sizeof(header)), 0);

    platform::zephyr::KeyValueStore key_value;
    zassert_equal(key_value.begin(), Status::Ok, "NVS did not come up over the ring");
    uint32_t first_word = 0;
    zassert_equal(flash_read(flash, ring_page, &first_word, sizeof(first_word)), 0);
    zassert_not_equal(first_word, platform::kWarmRingMagicV3, "the ring page survived");

    const uint8_t written[] = {1, 2, 3, 4};
    zassert_equal(key_value.write("settings", written, sizeof(written)), Status::Ok);
    uint8_t read[8] = {0};
    size_t stored_bytes = 0;
    zassert_equal(key_value.read("settings", read, sizeof(read), stored_bytes), Status::Ok);
    zassert_mem_equal(read, written, sizeof(written));
}

ZTEST(platform_adapters, test_missing_key_is_not_found) {
    platform::zephyr::KeyValueStore key_value;
    zassert_equal(key_value.begin(), Status::Ok);
    uint8_t read[4] = {0};
    size_t stored_bytes = 0;
    zassert_equal(key_value.read("nope", read, sizeof(read), stored_bytes), Status::NotFound);
}

namespace {

uint32_t waited_us(platform::zephyr::Delay& delay, uint32_t hold_us) {
    const uint32_t started_cycles = k_cycle_get_32();
    delay.wait_at_least_us(hold_us);
    return k_cyc_to_us_floor32(k_cycle_get_32() - started_cycles);
}

}  // namespace

ZTEST(platform_adapters, test_delay_sleeps_at_least_the_datasheet_windows) {
    platform::zephyr::Delay delay;
    const uint32_t windows_us[] = {10, 100, 500, 10000};
    for (const uint32_t window_us : windows_us)
        zassert_true(waited_us(delay, window_us) >= window_us, "asked for %u us", window_us);
}

ZTEST(platform_adapters, test_delay_with_interrupts_locked_spins_the_whole_window) {
    platform::zephyr::Delay delay;
    const unsigned int key = irq_lock();
    const uint32_t waited = waited_us(delay, 500);
    irq_unlock(key);
    zassert_true(waited >= 500, "waited %u us", waited);
}
