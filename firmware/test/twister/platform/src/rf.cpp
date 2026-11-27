#include "hardware/platform/zephyr/rf.h"

#include <zephyr/ztest.h>

#include "core/bus/bus.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/zephyr/clock.h"

using namespace skyblip;

namespace {

class PendingSx1262 : public models::Sx1262 {
   public:
    void transfer(const uint8_t* tx, uint8_t* rx, size_t bytes) override {
        models::Sx1262::transfer(tx, rx, bytes);
        if (!pend_on_stage_ || commands_seen.empty() ||
            commands_seen.back() != parts::sx1262::kWriteBuffer)
            return;
        pend_on_stage_ = false;
        k_sem_give(&staging_);
        (void)k_sem_take(&resume_, K_FOREVER);
    }

    void pend_on_next_stage() {
        k_sem_init(&staging_, 0, 1);
        k_sem_init(&resume_, 0, 1);
        pend_on_stage_ = true;
    }

    int wait_staging(k_timeout_t timeout) { return k_sem_take(&staging_, timeout); }
    void resume() { k_sem_give(&resume_); }

   private:
    volatile bool pend_on_stage_{false};
    struct k_sem staging_{};
    struct k_sem resume_{};
};

PendingSx1262 chip;
parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
platform::zephyr::Clock uptime;
bus::RfQueue events;
platform::zephyr::Rf rf(radio, uptime, events);
const uint8_t burst[24] = {0};

}  // namespace

ZTEST_SUITE(rf_executor, NULL, NULL, NULL, NULL, NULL);

// The power-off park can land while staging pends the radio thread on the SPI, as nrfx SPIM does.
ZTEST(rf_executor, test_a_park_during_staging_keys_no_burst) {
    zassert_equal(rf.begin(), Status::Ok, "the radio did not come up");
    chip.pend_on_next_stage();

    const uint64_t now_us = uptime.microseconds();
    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMBand;
    plan.start_us = now_us + 20000;
    plan.end_us = plan.start_us + 30000;
    plan.tx = burst;
    plan.tx_bytes = sizeof(burst);
    plan.tx_at_us = plan.start_us + 10000;
    plan.tx_forced = true;
    zassert_equal(rf.arm(plan), Status::Ok);

    zassert_equal(chip.wait_staging(K_MSEC(1000)), 0, "the burst was never staged");
    rf.sleep();
    chip.resume();
    for (int waited_ms = 0; !rf.parked() && waited_ms < 1000; waited_ms++) k_msleep(1);

    zassert_true(rf.parked(), "the radio thread never parked");
    zassert_false(chip.saw_command(parts::sx1262::kSetTx), "a burst keyed after the park");
}
