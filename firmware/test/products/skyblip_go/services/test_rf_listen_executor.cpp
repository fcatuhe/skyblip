// The executor's half of listen-before-talk: the CCA, its lead and dead time, force and withdrawal.
#include <initializer_list>

#include "core/events/rf.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "hardware/platform/clear_channel.h"
#include "simulator/simulator.h"
#include "test/support/rf_pass_rig.h"

using namespace skyblip;

namespace {

struct Executor {
    models::Sx1262 chip;
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf{radio, clock, events};
    uint8_t frame[adsl::Packet::kTxBytes] = {0x72, 0x4B};

    ports::RfPlan plan(uint64_t start_us, uint64_t tx_at_us, bool forced = false) {
        ports::RfPlan dwell{};
        dwell.mode = ports::RfMode::RxMBand;
        dwell.frequency_hz = adsl::kMBand0Hz;
        dwell.start_us = start_us;
        dwell.end_us = start_us + 345000;
        dwell.tx = frame;
        dwell.tx_bytes = sizeof(frame);
        dwell.tx_at_us = tx_at_us;
        dwell.tx_forced = forced;
        return dwell;
    }

    void run(uint32_t from_ms, uint32_t to_ms) {
        for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += 5) {
            clock.set_milliseconds(now_ms);
            rf.service(now_ms);
        }
    }

    // The silicon executor's own thread, which wakes to the microsecond.
    void run_us(uint64_t from_us, uint64_t to_us, int8_t (*level_at)(uint64_t) = nullptr) {
        for (uint64_t now_us = from_us; now_us <= to_us; now_us += 100) {
            if (level_at != nullptr) chip.rssi_dbm = level_at(now_us);
            clock.set_microseconds(now_us);
            rf.service(static_cast<uint32_t>(now_us / 1000));
        }
    }

    int busy_reports() {
        int busy = 0;
        events::RfEvent event{};
        while (events.pop(event)) busy += event.type == events::RfEventType::Busy ? 1 : 0;
        return busy;
    }

    bool keyed() const { return chip.saw_command(parts::sx1262::kSetTx); }
};

constexpr uint64_t kInstantUs = 600000;
constexpr int8_t kHeldDbm = -50;
constexpr int8_t kQuietDbm = -110;

}  // namespace

// EN 300 220-1 5.21.1: the executor listens before every burst, and a held channel keys nothing.
TEST_CASE("rf: a held channel reports the burst busy at its level, a quiet one keys it on time") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    const uint8_t frame[adsl::Packet::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan plan{};
    plan.mode = ports::RfMode::RxMBand;
    plan.frequency_hz = adsl::kMBand0Hz;
    plan.start_us = 450000;
    plan.end_us = 795000;
    plan.tx = frame;
    plan.tx_bytes = sizeof(frame);
    plan.tx_at_us = 600000;

    chip.rssi_dbm = -50;  // a neighbour holding the channel, 55 dB over the floor
    REQUIRE(rf.arm(plan) == Status::Ok);
    for (uint32_t now_ms = 450; now_ms <= 800; now_ms += 5) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
    }
    CHECK_FALSE(chip.saw_command(parts::sx1262::kSetTx));
    events::RfEvent event{};
    int busy = 0;
    while (events.pop(event)) {
        CHECK(event.type != events::RfEventType::Missed);
        if (event.type != events::RfEventType::Busy) continue;
        busy++;
        CHECK(event.tx_at_us == 600000);
        CHECK(event.rssi_valid);
        CHECK(event.rssi_dbm == -50);
    }
    CHECK(busy == 1);

    // The same dwell one second later on a quiet channel keys at its instant.
    chip.rssi_dbm = simulator::Air::kNoiseFloorDbm;
    plan.start_us += 1000000;
    plan.end_us += 1000000;
    plan.tx_at_us += 1000000;
    REQUIRE(rf.arm(plan) == Status::Ok);
    uint32_t keyed_at_ms = 0;
    for (uint32_t now_ms = 1450; now_ms <= 1800; now_ms += 5) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
        if (chip.tx_pending && keyed_at_ms == 0) keyed_at_ms = now_ms;
    }
    CHECK(keyed_at_ms == 1600);
    CHECK(radio.mode() == parts::RadioMode::Tx);
    CHECK(chip.tx_pending);
    // Two hundred milliseconds in Tx with no TxDone, and no firmware timer
    // anywhere: see the case below for why that is the right answer.
    CHECK(chip.tx_timeout_ticks != 0);
}

// ADS-L §D.3: "the device may force transmission irrespective of carrier detect".
TEST_CASE("rf: a forced burst keys on a held channel at its instant") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.chip.rssi_dbm = -50;
    REQUIRE(executor.rf.arm(executor.plan(450000, 600000, /*forced=*/true)) == Status::Ok);
    executor.run(450, 600);
    CHECK(executor.chip.tx_pending);
    events::RfEvent event{};
    while (executor.events.pop(event)) CHECK(event.type != events::RfEventType::Busy);
}

// The silence after a forced burst reaches a burst the service already handed over.
TEST_CASE("rf: a burst withdrawn before its instant never keys, one already keyed stays") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    REQUIRE(executor.rf.arm(executor.plan(450000, 600000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.plan(1450000, 1600000)) == Status::Ok);
    executor.run(450, 500);
    CHECK(executor.rf.withdraw(1600000) == Status::Ok);
    CHECK(executor.rf.withdraw(1600000) == Status::WouldBlock);
    executor.run(505, 600);
    CHECK(executor.chip.tx_pending);
    CHECK(executor.rf.withdraw(600000) == Status::WouldBlock);
    uint8_t sent[protocol::kTxPayloadChipBytes];
    uint8_t sent_bytes = 0;
    REQUIRE(executor.chip.take_tx(sent, sent_bytes));
    executor.run(605, 1800);
    CHECK_FALSE(executor.chip.tx_pending);

    REQUIRE(executor.rf.arm(executor.plan(2450000, 2600000)) == Status::Ok);
    executor.run(2450, 2500);
    CHECK(executor.rf.withdraw(2600000) == Status::Ok);
    executor.run(2505, 2800);
    CHECK_FALSE(executor.chip.tx_pending);
}

// EN 300 220-1 5.21.1 and Table 48: the window ends before the instant, inside the dead time.
TEST_CASE("rf: the executor listens ahead of the instant by the lead the device takes to stage") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    REQUIRE(executor.rf.arm(executor.plan(450000, kInstantUs)) == Status::Ok);
    // Held until a millisecond before the instant: a reading taken at the instant would key.
    executor.run_us(450000, kInstantUs + 1000, [](uint64_t now_us) {
        return now_us < kInstantUs - 1000 ? kHeldDbm : kQuietDbm;
    });
    CHECK(platform::kCcaLeadUs > 1000);
    CHECK_FALSE(executor.keyed());
    CHECK(executor.busy_reports() == 1);
}

TEST_CASE("rf: an executor that wakes past the dead time listens again before it keys") {
    for (const int8_t later : {kHeldDbm, kQuietDbm}) {
        Executor executor;
        REQUIRE(executor.rf.begin() == Status::Ok);
        REQUIRE(executor.rf.arm(executor.plan(450000, kInstantUs)) == Status::Ok);
        executor.chip.rssi_dbm = kQuietDbm;
        executor.run_us(450000, kInstantUs - platform::kCcaLeadUs);
        REQUIRE(executor.busy_reports() == 0);
        // Stalled 3 ms, past the 2 ms dead time, and the channel taken meanwhile or not.
        executor.chip.rssi_dbm = later;
        executor.run_us(kInstantUs + 3000 - platform::kCcaLeadUs, kInstantUs + 3000);
        CHECK(executor.keyed() == (later == kQuietDbm));
        CHECK(executor.busy_reports() == (later == kHeldDbm ? 1 : 0));
    }
}

// The silence after a forced burst withdraws what it can: a burst under assessment is past that.
TEST_CASE("rf: a burst whose assessment has begun can no longer be withdrawn, and keys") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.chip.rssi_dbm = kQuietDbm;
    REQUIRE(executor.rf.arm(executor.plan(450000, kInstantUs)) == Status::Ok);
    executor.run_us(450000, kInstantUs - 1000);
    CHECK(executor.rf.withdraw(kInstantUs) == Status::WouldBlock);
    executor.run_us(kInstantUs - 900, kInstantUs);
    CHECK(executor.keyed());
}

// E1. A jammed site keys no airborne position: each is deferred, then missed, never forced.
TEST_CASE("rf: a jammed site defers every airborne position, and reports the floor it measured") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);
    CHECK(pass.radio_service.noise_floor().level_dbm() == timing::NoiseFloor::kSeedDbm);

    pass.chip.rssi_dbm = -50;  // a neighbour sitting on the channel, 55 dB over the floor
    for (uint64_t now_us = 0; now_us <= 2900000; now_us += 10000) pass.whole_pass(now_us);

    CHECK_FALSE(pass.chip.saw_command(parts::sx1262::kSetTx));
    CHECK(pass.state.air.tx_deferred > 0);
    CHECK(pass.state.air.tx_busy_missed > 0);
    CHECK(pass.state.air.tx_forced == 0);
    CHECK(pass.state.rf.timing_statistics.refused() == 0);
    // The floor is a measurement, and a site this loud has walked it off the seed.
    CHECK(pass.radio_service.noise_floor().samples() > 3);
    CHECK(pass.radio_service.noise_floor().level_dbm() > timing::NoiseFloor::kSeedDbm);
}

// A read at one instant used to license a burst on top of a neighbour it did not land on.
TEST_CASE("rf: the floor and the clear channel assessment are each a window of reads") {
    Pass pass;
    REQUIRE(pass.begin() == Status::Ok);

    // A quiet channel with a neighbour occupying one ninth of every window.
    const int8_t window[timing::ChannelLevel::kSamples] = {-115, -115, -115, -115, -45,
                                                           -115, -115, -115, -115};
    pass.chip.set_rssi_sequence(window, timing::ChannelLevel::kSamples);
    for (uint64_t now_us = 0; now_us <= 2900000; now_us += 10000) pass.whole_pass(now_us);
    // The floor the service averages is the window's figure, not the quiet read.
    CHECK(pass.radio_service.noise_floor().level_dbm() > window[0] + 20);

    // The same channel read only at its quiet instant keys the PA.
    Pass single;
    REQUIRE(single.begin() == Status::Ok);
    single.chip.rssi_dbm = window[0];
    for (uint64_t now_us = 0; now_us <= 2900000; now_us += 10000) single.whole_pass(now_us);
    CHECK(single.radio_service.noise_floor().level_dbm() < timing::NoiseFloor::kSeedDbm);
    CHECK(pass.radio_service.noise_floor().level_dbm() >
          single.radio_service.noise_floor().level_dbm() + 10);
    CHECK(single.chip.saw_command(parts::sx1262::kSetTx));
    CHECK(single.state.air.tx_deferred == 0);
    // The windowed assessment heard the neighbour the quiet read missed, so it never keyed.
    CHECK_FALSE(pass.chip.saw_command(parts::sx1262::kSetTx));
    CHECK(pass.state.air.tx_deferred > 0);
}
