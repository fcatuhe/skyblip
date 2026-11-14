// Listen before talk as flown: ADS-L §D.3 over EN 300 220-1 5.21, from the assessment to the air.
#include <cstring>
#include <vector>

#include "core/protocol/adsl.h"
#include "core/settings/address.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
#include "protocols/adsl/packet.h"
#include "simulator/simulator.h"
#include "test/support/armings_rig.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

using Payload = timing::Transmitter::Payload;

constexpr uint32_t kStepMs = 1;
constexpr uint64_t kAirTimeUs = timing::Transmitter::kAirTimeMs * 1000;
constexpr uint32_t kWaitCeilingMs = 30000;
const int8_t kHeldDbm[1] = {-50};

// A Go in the simulator, stepped a millisecond at a time, whose channel a case can hold busy.
struct Listening {
    simulator::Simulator simulator;
    uint32_t now_ms{0};

    explicit Listening(int speed_kt, const char* callsign = "") {
        REQUIRE(simulator.setup() == Status::Ok);
        std::strncpy(simulator.product().settings().callsign, callsign, go::kCallsignBytes - 1);
        simulator.world().set_fix(true);
        simulator.world().set_speed_kt(speed_kt);
        now_ms = past_settling(simulator);
    }

    const bus::State& state() { return simulator.product().state(); }
    const timing::Transmitter& transmitter() { return simulator.product().radio().transmitter(); }
    void hold(bool held) {
        simulator.platform().chips().radio.set_rssi_sequence(kHeldDbm, held ? 1 : 0);
    }
    void step() {
        now_ms += kStepMs;
        simulator.step(now_ms);
    }
    void run_for(uint32_t for_ms) {
        const uint32_t until_ms = now_ms + for_ms;
        while (now_ms < until_ms) step();
    }
    int phase() const { return static_cast<int>(now_ms % 1000); }
    uint32_t cycle_second() {
        const uint32_t second = state().traffic_now_s(now_ms);
        return (second % timing::Transmitter::kCycleS + timing::Transmitter::kCycleS -
                transmitter().cycle_start()) %
               timing::Transmitter::kCycleS;
    }
    // The top of the next second that sits `offset` seconds into this unit's cycle.
    void to_cycle_second(uint32_t offset) {
        const uint32_t until_ms = now_ms + kWaitCeilingMs;
        while (now_ms < until_ms && !(phase() == 0 && cycle_second() == offset)) step();
        REQUIRE(phase() == 0);
    }
    bool logged(radio::Verdict verdict) {
        const radio::Log& log = state().radio_log;
        for (int i = 0; i < log.count(); i++)
            if (log.newest(i).verdict == verdict) return true;
        return false;
    }
    std::vector<simulator::AirRecord> sent() {
        std::vector<simulator::AirRecord> records;
        const simulator::Air& air = simulator.world().air();
        for (int i = 0; i < air.record_count(); i++)
            if (air.record_at(i).event == simulator::AirEvent::Tx)
                records.push_back(air.record_at(i));
        return records;
    }
};

uint64_t ms_to_us(uint32_t ms) { return static_cast<uint64_t>(ms) * 1000; }

ports::RfPlan last_with_burst(const Armings& armings) {
    ports::RfPlan found{};
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].tx != nullptr) found = armings.rf.plans[i];
    return found;
}

// Which dwell a burst went out in: slot 0 or slot 1 of the second each opens in.
uint64_t dwell_of(uint64_t at_us) {
    const uint64_t from_slot0_ms = at_us / 1000 - timing::kSlot0StartMs;
    return from_slot0_ms / 1000 * 2 + (from_slot0_ms % 1000 >= 400 ? 1 : 0);
}

}  // namespace

TEST_CASE("rf: on a clear channel nothing is deferred and every burst keys at its instant") {
    Listening run(50, "D-KXYZ");
    run.run_for(21000);
    CHECK(run.state().air.tx_deferred == 0);
    CHECK(run.state().air.tx_busy_missed == 0);
    CHECK(run.state().air.tx_forced == 0);
    CHECK(count_payloads(run.simulator.world().air(), adsl::Packet::kPayloadTypePosition) >= 20);
    const radio::Log& log = run.state().radio_log;
    for (int i = 0; i < log.count(); i++) {
        if (log.newest(i).verdict != radio::Verdict::Transmitted) continue;
        CHECK(log.newest(i).tx_keyed_us < kStepMs * 1000);
    }
}

TEST_CASE("rf: a busy channel defers the burst, which keys later inside its own window") {
    Listening run(50);
    run.run_for(2000);
    run.hold(true);
    const uint32_t until_ms = run.now_ms + kWaitCeilingMs;
    while (run.state().air.tx_deferred == 0 && run.now_ms < until_ms) run.step();
    run.hold(false);
    REQUIRE(run.state().air.tx_deferred == 1);
    const uint64_t busy_at_us = run.state().rf.busy_at(run.state().rf.busy_reports - 1)->tx_at_us;
    const radio::Entry& row = run.state().radio_log.newest(0);
    CHECK(row.verdict == radio::Verdict::Deferred);
    CHECK(row.rssi_valid);
    CHECK(row.rssi_dbm == kHeldDbm[0]);
    CHECK(row.into_ms == busy_at_us / 1000 % 1000);
    run.run_for(1500);

    const simulator::AirRecord* deferred = nullptr;
    const std::vector<simulator::AirRecord> sent = run.sent();
    for (const simulator::AirRecord& record : sent)
        if (record.at_us > busy_at_us && deferred == nullptr) deferred = &record;
    REQUIRE(deferred != nullptr);
    const int busy_slot = timing::Scheduler::slot_of(static_cast<int>(busy_at_us / 1000 % 1000));
    CHECK(simulator::Air::tuned_to(deferred->frequency_hz,
                                   timing::Scheduler::slot_frequency_hz(busy_slot)));
    CHECK(dwell_of(deferred->at_us) == dwell_of(busy_at_us));
    CHECK((deferred->at_us - busy_at_us) % timing::PoliteAccess::kDeferralUnitUs == 0);
    CHECK(timing::Scheduler::in_direct_slot(deferred->phase_ms));
    CHECK(deferred->phase_ms + timing::Transmitter::kAirTimeMs <= adsl::kDirectEndMs);
    CHECK(run.state().air.tx_busy_missed == 0);

    // G.1.1: the timestamp names the instant the burst went out at, not the one it was drawn for.
    adsl::Packet packet{};
    REQUIRE(packet_of(*deferred, packet));
    CHECK(packet.payload_type == adsl::Packet::kPayloadTypePosition);
    const uint32_t utc_s = run.state().traffic_now_s(run.now_ms) -
                           (run.now_ms / 1000 - static_cast<uint32_t>(deferred->at_us / 1000000));
    CHECK(packet.timestamp == adsl::timestamp_code(utc_s, deferred->phase_ms));
}

TEST_CASE("rf: a ground burst busy its whole window is missed, and goes 2 s on, same channel") {
    Listening run(0);
    run.to_cycle_second(timing::Transmitter::kPositionOffsetsS[0]);
    const uint64_t second_us = ms_to_us(run.now_ms);
    run.hold(true);
    run.run_for(1000);
    run.hold(false);
    CHECK(run.state().air.tx_deferred > 0);
    CHECK(run.state().air.tx_busy_missed == 1);
    CHECK(run.state().radio_log.newest(0).verdict == radio::Verdict::Busy);
    CHECK(run.logged(radio::Verdict::Deferred));
    run.run_for(3000);

    int missed_second = 0, retried = 0;
    for (const simulator::AirRecord& record : run.sent()) {
        if (payload_type_of(record) != adsl::Packet::kPayloadTypePosition) continue;
        if (record.at_us >= second_us && record.at_us < second_us + 1000000) missed_second++;
        if (record.at_us >= second_us + 2000000 && record.at_us < second_us + 3000000) {
            retried++;
            CHECK(simulator::Air::tuned_to(record.frequency_hz, adsl::kMBand0Hz));
        }
    }
    CHECK(missed_second == 0);
    CHECK(retried == 1);
}

// An airborne position's next second covers it: one held off is neither retried nor forced.
TEST_CASE("rf: an airborne position is never retried and never forced, however long it waits") {
    Listening run(50);
    run.to_cycle_second(timing::Transmitter::kStatusOffsetsS[0] + 1);
    const uint64_t held_from_us = ms_to_us(run.now_ms);
    run.hold(true);
    run.run_for(4000);
    run.hold(false);
    const uint64_t held_until_us = ms_to_us(run.now_ms);
    run.run_for(3000);

    CHECK(run.state().air.tx_forced == 0);
    CHECK(run.state().air.tx_busy_missed >= 4);
    std::vector<uint64_t> seconds;
    for (const simulator::AirRecord& record : run.sent()) {
        if (record.at_us >= held_from_us && record.at_us < held_until_us) FAIL("keyed while held");
        if (record.at_us < held_until_us) continue;
        if (payload_type_of(record) != adsl::Packet::kPayloadTypePosition) continue;
        adsl::Packet packet{};
        REQUIRE(packet_of(record, packet));
        seconds.push_back(record.at_us / 1000000);
    }
    CHECK(seconds.size() == 3);
    for (size_t i = 1; i < seconds.size(); i++) CHECK(seconds[i] == seconds[i - 1] + 1);
}

TEST_CASE("rf: on the ground a burst busy for 3000 ms is forced, then 2000 ms of nothing ours") {
    Listening run(0, "D-KXYZ");
    run.to_cycle_second(timing::Transmitter::kPositionOffsetsS[0]);
    const uint64_t held_from_us = ms_to_us(run.now_ms);
    run.hold(true);
    bool forced_logged = false;
    for (uint32_t step = 0; step < 12000; step++) {
        run.step();
        forced_logged = forced_logged || run.logged(radio::Verdict::Forced);
    }
    run.hold(false);

    CHECK(forced_logged);
    CHECK(run.state().air.tx_forced >= 2);
    CHECK(run.state().air.tx_deferred > 0);
    CHECK(run.state().air.tx_busy_missed > 0);
    REQUIRE(run.simulator.product().radio().forced_at_us().has_value());

    std::vector<simulator::AirRecord> forced;
    for (const simulator::AirRecord& record : run.sent())
        if (record.at_us >= held_from_us) forced.push_back(record);
    REQUIRE(forced.size() >= 2);
    // Nothing clears, so the first burst on air waited out 3000 ms since an initial attempt.
    if (!forced.empty()) CHECK(forced[0].at_us >= held_from_us + adsl::kForceAfterAttemptUs);
    for (size_t i = 1; i < forced.size(); i++)
        CHECK(forced[i].at_us - forced[i - 1].at_us >= kAirTimeUs + adsl::kSilenceAfterForceUs);
}

// The channel clears the moment the force is decided, so only the silence keeps ours off the air.
TEST_CASE("rf: after a forced burst ends nothing of ours goes on air for 2000 ms, clear or not") {
    Listening run(0, "D-KXYZ");
    run.to_cycle_second(timing::Transmitter::kPositionOffsetsS[0]);
    const uint64_t held_from_us = ms_to_us(run.now_ms);
    run.hold(true);
    const uint32_t until_ms = run.now_ms + 12000;
    while (run.state().air.tx_forced == 0 && run.now_ms < until_ms) run.step();
    run.hold(false);
    REQUIRE(run.state().air.tx_forced == 1);
    run.run_for(6000);

    std::vector<simulator::AirRecord> sent;
    for (const simulator::AirRecord& record : run.sent())
        if (record.at_us >= held_from_us) sent.push_back(record);
    REQUIRE(sent.size() >= 2);
    if (sent.size() < 2) return;
    CHECK(sent[0].at_us == *run.simulator.product().radio().forced_at_us());
    CHECK(sent[1].at_us >= sent[0].at_us + kAirTimeUs + adsl::kSilenceAfterForceUs);
}

// Without a name to force, the turn that waits longest on the ground is a position.
TEST_CASE("rf: on the ground with no name set, a position busy for 3000 ms is forced") {
    Listening run(0);
    run.to_cycle_second(timing::Transmitter::kPositionOffsetsS[0]);
    const uint64_t held_from_us = ms_to_us(run.now_ms);
    run.hold(true);
    run.run_for(6000);
    run.hold(false);
    CHECK(run.state().air.tx_forced >= 1);
    std::vector<simulator::AirRecord> sent;
    for (const simulator::AirRecord& record : run.sent())
        if (record.at_us >= held_from_us) sent.push_back(record);
    REQUIRE_FALSE(sent.empty());
    if (sent.empty()) return;
    CHECK(payload_type_of(sent[0]) == adsl::Packet::kPayloadTypePosition);
    CHECK(sent[0].at_us >= held_from_us + adsl::kForceAfterAttemptUs);
}

// EN 300 220-1 Table 48's off-time and the dwell map's one burst a dwell, with deferrals in play.
TEST_CASE("rf: deferred bursts keep 100 ms apart on a frequency, and one to a dwell") {
    Listening run(50, "D-KXYZ");
    const uint32_t until_ms = run.now_ms + 42000;
    while (run.now_ms < until_ms) {
        run.hold(run.now_ms % 47 < 9);
        run.step();
    }
    run.hold(false);
    CHECK(run.state().air.tx_deferred >= 5);

    const std::vector<simulator::AirRecord> sent = run.sent();
    REQUIRE(sent.size() > 30);
    for (size_t i = 0; i < sent.size(); i++) {
        for (size_t j = i + 1; j < sent.size(); j++) {
            CHECK(dwell_of(sent[i].at_us) != dwell_of(sent[j].at_us));
            if (sent[i].frequency_hz != sent[j].frequency_hz) continue;
            const uint64_t ended_us = sent[i].at_us + timing::Transmitter::kAirTimeMs * 1000;
            CHECK(sent[j].at_us - ended_us >=
                  ms_to_us(timing::PoliteAccess::kSameFrequencyOffFloorMs));
        }
    }
}

// G.1.16 with §D.3: the deferred burst is built again for its new instant, from the newest fix.
TEST_CASE("rf: a deferred position is re-encoded at its new instant, byte for byte") {
    Armings armings;
    armings.ready_to_transmit(1000);
    armings.state.own.latitude_1e7 = 455000000;
    armings.state.own.longitude_1e7 = 60000000;
    armings.state.own.speed_mm_s = 120000;
    armings.state.own.track_cdeg = 4500;
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const ports::RfPlan drawn = last_with_burst(armings);

    // A newer fix lands while the burst waits, a kilometre north: only a fresh encoding carries it.
    const std::vector<uint8_t> drawn_chips(drawn.tx, drawn.tx + drawn.tx_bytes);
    armings.state.own.latitude_1e7 += 90000;
    armings.state.rf.note_busy({drawn.tx_at_us, -60, true});
    const int busy_phase = static_cast<int>((drawn.tx_at_us - Armings::kEdgeUs) / 1000);
    armings.tick_in(0, busy_phase + 1);
    REQUIRE(armings.rf.with_tx == 2);
    const ports::RfPlan deferred = last_with_burst(armings);
    CHECK(deferred.tx_at_us > drawn.tx_at_us);
    CHECK((deferred.tx_at_us - drawn.tx_at_us) % timing::PoliteAccess::kDeferralUnitUs == 0);
    CHECK_FALSE(deferred.tx_forced);
    CHECK(armings.state.air.tx_deferred == 1);

    // On air the radio puts its sync word in front of the chips it was handed, as the model does.
    simulator::AirRecord record{};
    record.event = simulator::AirEvent::Tx;
    const uint8_t sync_bytes = deferred.sync_bits / 8;
    std::memcpy(record.chips, deferred.sync, sync_bytes);
    std::memcpy(record.chips + sync_bytes, deferred.tx, deferred.tx_bytes);
    record.bytes = static_cast<uint16_t>(sync_bytes + deferred.tx_bytes);
    adsl::Packet sent{};
    REQUIRE(packet_of(record, sent));

    const int32_t at_ms = static_cast<int32_t>((deferred.tx_at_us - Armings::kEdgeUs) / 1000);
    protocol::BurstInstant instant{};
    instant.utc_s = armings.state.traffic_now_s(static_cast<uint32_t>(Armings::kEdgeUs / 1000));
    instant.into_utc_ms = at_ms;
    instant.since_fix_ms =
        static_cast<int32_t>(deferred.tx_at_us / 1000 - armings.state.own.fix_at_ms);
    adsl::Packet expected{};
    REQUIRE(protocol::from_own(expected, armings.state.own, armings.state.activation.address(),
                               settings::kSkyblipAddressMappingTable,
                               armings.state.own.aircraft_category, instant));
    CHECK(std::memcmp(sent.data, expected.data, adsl::kFrameBytes - 3) == 0);
    CHECK(std::memcmp(drawn_chips.data(), deferred.tx, deferred.tx_bytes) != 0);
}

TEST_CASE("rf: a burst busy at the end of its window is missed, never armed past it") {
    Armings armings;
    armings.ready_to_transmit(1000);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.tick_in(0, 420);
    REQUIRE(armings.rf.with_tx == 1);
    const ports::RfPlan drawn = last_with_burst(armings);

    // Heard a pass too late to listen again inside slot 0's direct window.
    armings.state.rf.note_busy({drawn.tx_at_us, -60, true});
    armings.tick_in(0, timing::Transmitter::last_instant(Payload::Position, 0));
    CHECK(armings.rf.with_tx == 1);
    CHECK(armings.state.air.tx_busy_missed == 1);
    CHECK(armings.state.air.tx_deferred == 0);
}
