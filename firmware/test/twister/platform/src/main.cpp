// What the pure-host suite cannot reach: the Zephyr-backed adapters themselves.
// Runs under native_sim in CI and on the board when one is attached.
#include <zephyr/ztest.h>

#include "hardware/platform/zephyr/io.h"
#include "hardware/platform/zephyr/kvstore.h"

using namespace skyblip;

ZTEST_SUITE(platform_adapters, NULL, NULL, NULL, NULL, NULL);

ZTEST(platform_adapters, test_kvstore_round_trips_through_nvs) {
    platform::zephyr::KvStore kv;
    zassert_equal(kv.begin(), Status::Ok, "NVS did not come up");

    const uint8_t written[] = {1, 2, 3, 4};
    zassert_equal(kv.write("settings", written, sizeof(written)), Status::Ok);

    uint8_t read[8] = {0};
    size_t len = 0;
    zassert_equal(kv.read("settings", read, sizeof(read), len), Status::Ok);
    zassert_equal(len, sizeof(written));
    zassert_mem_equal(read, written, sizeof(written));
}

ZTEST(platform_adapters, test_missing_key_is_not_found) {
    platform::zephyr::KvStore kv;
    zassert_equal(kv.begin(), Status::Ok);
    uint8_t read[4] = {0};
    size_t len = 0;
    zassert_equal(kv.read("nope", read, sizeof(read), len), Status::NotFound);
}

namespace {

uint32_t waited_us(platform::zephyr::Delay& delay, uint32_t us) {
    const uint32_t from = k_cycle_get_32();
    delay.wait_at_least_us(us);
    return k_cyc_to_us_floor32(k_cycle_get_32() - from);
}

}  // namespace

ZTEST(platform_adapters, test_delay_sleeps_at_least_the_datasheet_windows) {
    platform::zephyr::Delay delay;
    const uint32_t windows_us[] = {10, 100, 500, 10000};
    for (const uint32_t us : windows_us)
        zassert_true(waited_us(delay, us) >= us, "asked for %u us", us);
}

ZTEST(platform_adapters, test_delay_with_interrupts_locked_spins_the_whole_window) {
    platform::zephyr::Delay delay;
    const unsigned int key = irq_lock();
    const uint32_t waited = waited_us(delay, 500);
    irq_unlock(key);
    zassert_true(waited >= 500, "waited %u us", waited);
}
