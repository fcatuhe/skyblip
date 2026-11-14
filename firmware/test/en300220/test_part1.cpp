// ETSI EN 300 220-1 V3.1.1 clause 5.21, polite spectrum access, read against this device.
#include <cstdint>
#include <cstring>
#include <set>

#include "core/events/rf.h"
#include "core/timing/channel.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/rf.h"
#include "protocols/adsl/media_access.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// The host executor alone, one burst armed on channel A at 600 ms, the channel read at `level_dbm`.
struct Burst {
    models::Sx1262 chip;
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf{radio, clock, events};
    uint8_t frame[adsl::Packet::kTxBytes] = {0x72, 0x4B};
    bool busy{false};

    explicit Burst(int8_t level_dbm) {
        REQUIRE(rf.begin() == Status::Ok);
        ports::RfPlan plan{};
        plan.mode = ports::RfMode::RxMBand;
        plan.frequency_hz = adsl::kMBand0Hz;
        plan.start_us = 450000;
        plan.end_us = 795000;
        plan.tx = frame;
        plan.tx_bytes = sizeof(frame);
        plan.tx_at_us = 600000;
        chip.rssi_dbm = level_dbm;
        REQUIRE(rf.arm(plan) == Status::Ok);
        for (uint32_t now_ms = 450; now_ms <= 800; now_ms += 5) {
            clock.set_milliseconds(now_ms);
            rf.service(now_ms);
        }
        events::RfEvent event{};
        while (events.pop(event)) busy = busy || event.type == events::RfEventType::Busy;
    }

    bool keyed() const { return chip.saw_command(parts::sx1262::kSetTx); }
};

}  // namespace

TEST_CASE("EN 300 220-1 5.21.1: a burst goes out below the CCA threshold and defers above it") {
    const Burst quiet(-110);
    CHECK(quiet.keyed());
    CHECK_FALSE(quiet.busy);
    const Burst held(-50);
    CHECK_FALSE(held.keyed());
    CHECK(held.busy);

    // "a random interval ... (multiple of the minimum deferral period)", drawn per busy instant.
    timing::Transmitter transmitter;
    transmitter.configure(0x5B7E57);
    std::set<uint64_t> waits;
    for (uint64_t busy_us = 10000000; busy_us < 10400000; busy_us += 7000) {
        adsl::ChannelAttempt attempt{};
        attempt.at_us = busy_us;
        attempt.first_us = busy_us;
        attempt.earliest_us = busy_us;
        attempt.last_us = busy_us + 300000;
        const adsl::AccessVerdict verdict = adsl::media_access(
            attempt, {timing::PoliteAccess::kDeferralUnitUs, timing::PoliteAccess::kDeferralUnits,
                      transmitter.deferral_draw(busy_us)});
        REQUIRE(verdict.access == adsl::Access::Defer);
        const uint64_t wait_us = verdict.at_us - busy_us;
        CHECK(wait_us % timing::PoliteAccess::kDeferralUnitUs == 0);
        CHECK(wait_us >= 160);
        waits.insert(wait_us);
    }
    CHECK(waits.size() == timing::PoliteAccess::kDeferralUnits);
}

// Table 45 for under 100 mW e.r.p., over Table 32's S = 10 log 200 kHz - 117 dBm, at 0 dBd.
TEST_CASE(
    "EN 300 220-1 5.21.2.2: the CCA threshold is 15 dB over the sensitivity limit, at the "
    "front end") {
    CHECK(timing::cca_threshold_centi_dbm(0) == -9399 + 1500);
    const int32_t gain_centi_dbd = parts::sx1262::kReceiveGainCentiDbd;
    const int32_t threshold = platform::host::Rf::kCcaThresholdCentiDbm;
    CHECK(threshold == -7899 + gain_centi_dbd);
    // Referred back to a 0 dBd antenna, as Table 45's note asks, the threshold is the limit.
    CHECK(threshold - gain_centi_dbd <= -7899);
    CHECK(gain_centi_dbd == 160 - 215 - 50);

    const int8_t loudest_clear_dbm = static_cast<int8_t>((threshold - 99) / 100);
    CHECK(Burst(loudest_clear_dbm).keyed());
    CHECK_FALSE(Burst(static_cast<int8_t>(loudest_clear_dbm + 1)).keyed());
}

TEST_CASE("EN 300 220-1 5.21.3.1: listening, dead time, on-time and off-time sit inside Table 48") {
    CHECK(timing::PoliteAccess::kCcaWindowUs >= 160);
    CHECK(timing::ChannelLevel::kWindowUs >= 160);
    CHECK(timing::PoliteAccess::kDeferralFloorUs >= timing::PoliteAccess::kCcaWindowUs);
    CHECK(timing::PoliteAccess::kDeadTimeCeilingUs <= 5000);
    CHECK(timing::PoliteAccess::kTransmissionCeilingUs <= 1000000);
    // 1 % of the hour across both channels is under 100 s an hour on any 200 kHz of them.
    CHECK(timing::AirTime::kBudgetMs <= 100000);

    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 21000, 1);
    const simulator::Air& air = simulator.world().air();
    int sent = 0;
    uint64_t last_end_us[2] = {0, 0};
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        sent++;
        CHECK(simulator::Air::air_time_us(record.bytes, record.chip_rate_bps) <= 1000000u);
        const int channel = simulator::Air::tuned_to(record.frequency_hz, adsl::kMBand1Hz) ? 1 : 0;
        if (last_end_us[channel] != 0) CHECK(record.at_us - last_end_us[channel] >= 100000);
        last_end_us[channel] =
            record.at_us + simulator::Air::air_time_us(record.bytes, record.chip_rate_bps);
    }
    CHECK(sent >= 22);
}

TEST_CASE("EN 300 220-1 5.21.3.2: the seven declarations are made, each within its limit") {
    // 1) the minimum CCA interval and 2) the maximum dead time.
    CHECK(timing::PoliteAccess::kCcaWindowUs == 200);
    CHECK(timing::PoliteAccess::kDeadTimeCeilingUs == 2000);
    // 3) the draw changes with the busy instant and with the device.
    timing::Transmitter one;
    one.configure(0x5B7E57);
    timing::Transmitter other;
    other.configure(0x5B01A0);
    CHECK(one.deferral_draw(10000000) != one.deferral_draw(10005000));
    CHECK(one.deferral_draw(10000000) != other.deferral_draw(10000000));
    // 4) the unit is one ADS-L burst, 5) a deferral draws one to eight of them.
    CHECK(timing::PoliteAccess::kDeferralUnitUs == 5000);
    CHECK(timing::PoliteAccess::kDeferralFloorUs == 5000);
    CHECK(timing::PoliteAccess::kDeferralCeilingUs == 40000);
    // 6) Ton max is one 4.64 ms burst, booked as 5 ms, 7) Toff min is 100 ms.
    CHECK(timing::PoliteAccess::kTransmissionCeilingUs == 5000);
    CHECK(timing::PoliteAccess::kSameFrequencyOffFloorMs == 100);
}

// INFO: fc 14nov26 optional: 5.21.1 offers AFA as an alternative, ADS-L's second names the channel
TEST_CASE("EN 300 220-1 5.21.4.1: a busy channel is left for another operating frequency" *
          doctest::skip()) {
    FAIL(
        "optional: no AFA, a deferred burst keeps the channel C.2.5's alternation gave its second");
}
