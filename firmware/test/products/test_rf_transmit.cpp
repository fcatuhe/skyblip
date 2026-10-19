// What own-ship puts on air: the rate, the window, the channel, the callsign second and the fix
// a burst carries, and all of it across the 49.7-day wrap.
#include <cstring>
#include <string>

#include "core/events/rf.h"
#include "core/gnss/first_fix.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/timing/slot.h"
#include "core/timing/slot_timing_statistics.h"
#include "core/timing/transmit.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/rf_channel.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

// What own-ship had already spent when a case starts measuring.
struct Spent {
    uint32_t tx_ok{0};
    uint32_t air_time_ms{0};
};

Spent spent_so_far(simulator::Simulator& simulator) {
    return {simulator.product().state().air.tx_ok,
            simulator.product().radio().transmitter().air_time().total_ms()};
}

}  // namespace

TEST_CASE("rf: own-ship transmits once a second, inside its window, alternating channel") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(simulator);
    const Spent before = spent_so_far(simulator);
    run_on(simulator, from_ms, 6000);

    const simulator::Air& air = simulator.world().air();
    int transmissions = 0;
    uint32_t last_frequency = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        transmissions++;
        // Always inside the direct slot, even though the dwell that carries the
        // burst runs 200 ms past it.
        CHECK(timing::Scheduler::in_direct_slot(record.phase_ms));
        CHECK(record.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kDirectEndMs);
        if (last_frequency != 0) CHECK(record.frequency_hz != last_frequency);
        last_frequency = record.frequency_hz;
    }
    // Six seconds of flight, one burst a second, less the one the cleared tape cut in half.
    CHECK(transmissions >= 5);
    CHECK(simulator.product().state().air.tx_ok - before.tx_ok ==
          static_cast<uint32_t>(transmissions));

    // E1 and E2, read off the service that spent them: the floor the carrier
    // sense threshold is derived from, and every millisecond that went on air.
    CHECK(simulator.product().radio().noise_floor().samples() > 0);
    CHECK(simulator.product().radio().noise_floor().level_dbm() < timing::NoiseFloor::kSeedDbm);
    CHECK(simulator.product().radio().transmitter().air_time().total_ms() - before.air_time_ms ==
          static_cast<uint32_t>(transmissions) * timing::Transmitter::kAirTimeMs);
    CHECK(simulator.product().radio().duty_permille(from_ms + 6000) <
          timing::AirTime::kLimitPermille);
    CHECK_FALSE(simulator.product().radio().over_budget());
}

// A step between the product keying and the air taking the burst spent 5 of a burst's 7 ms slack.
TEST_CASE("rf: the simulator puts own-ship's burst on air in the step that keyed it") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    run_on(simulator, past_settling(simulator), 6000);

    const radio::Log& log = simulator.product().state().radio_log;
    int done = 0;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.verdict != radio::Verdict::Transmitted) continue;
        done++;
        // 4.8 ms of air from the keying step, read out on the next one.
        CHECK(entry.tx_span_us - entry.tx_keyed_us <= simulator::Simulator::kStepMs * 1000);
    }
    CHECK(done >= 5);
}

// §C.5 reserves 0..200 and this is the one burst that goes there: core/timing/README.md.
TEST_CASE("rf: the callsign goes out in slot 1's tail, once every ten seconds, on channel 1") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(simulator);
    run_on(simulator, from_ms, 21000);

    const simulator::Air& air = simulator.world().air();
    int named = 0;
    int positions = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        if (timing::Scheduler::in_direct_slot(record.phase_ms)) {
            positions++;
            continue;
        }
        named++;
        CHECK(record.phase_ms < timing::kSlot1WrapMs);
        CHECK(record.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kCallsignEndMs - 1000);
        CHECK(simulator::Air::tuned_to(record.frequency_hz, timing::kMBand1Hz));
    }
    // Twenty-one seconds of flight: twenty-one positions and two names.
    CHECK(positions >= 19);
    CHECK(named == 2);

    // And the tape says which burst was which, where a position prints its rate.
    const radio::Log& log = simulator.product().state().radio_log;
    int rows = 0;
    for (int i = 0; i < log.count(); i++)
        if (log.newest(i).verdict == radio::Verdict::Transmitted && log.newest(i).callsign) rows++;
    CHECK(rows > 0);
}

// The default is a device nobody has named, and a nameless burst would say nothing.
TEST_CASE("rf: a device with no callsign set transmits nothing in the reserved window") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    REQUIRE(simulator.product().settings().callsign[0] == 0);
    run_on(simulator, past_settling(simulator), 21000);

    const simulator::Air& air = simulator.world().air();
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        CHECK(timing::Scheduler::in_direct_slot(record.phase_ms));
    }
}

TEST_CASE("rf: what own-ship put on air decodes back to own-ship state") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    simulator.world().set_altitude_m(1200);
    run_on(simulator, past_settling(simulator), 3000);

    const simulator::Air& air = simulator.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        // Own bursts go on air as chips behind the ADS-L sync word, so reading
        // them back means framing them the way the receiver does.
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(record, frame));
        REQUIRE(frame.system == protocol::System::AdslDirect);
        protocol::AdslPacket packet{};
        packet.reset();
        std::memcpy(packet.data, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(packet.check_crc() == 0);
        packet.descramble();
        CHECK(packet.address() == settings::address_of(platform::host::Identity::kSerial));
        CHECK(packet.altitude_m() ==
              to_metres(Millimetres(simulator.product().state().own.altitude_hae_mm)).value);
        CHECK(packet.flight_state == 2);
        checked++;
    }
    CHECK(checked > 0);
}

// G6's wiring, not the bucket arithmetic (core/test_timing.cpp already pins
// that down): RadioService owns the deadline, TrafficService the executor's
// report, and this proves the two actually meet in state.rf.timing_statistics rather
// than each keeping a private opinion.
TEST_CASE("rf: a completed burst lands in the bench's dwell-phase histogram") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    // 1 ms passes: a 5 ms one spends the burst's completion slack before the radio is serviced.
    run_on(simulator, past_settling(simulator), 6000, 1);

    const timing::SlotTimingStatistics& statistics =
        simulator.product().state().rf.timing_statistics;
    CHECK(statistics.dwell_samples() > 0);
    CHECK(statistics.missed() == 0);
    CHECK(statistics.refused() == 0);
    // Never before the instant it was armed for, and never as late as the burst is long.
    CHECK(statistics.dwell_worst_us() >= 0);
    CHECK(statistics.dwell_worst_us() <
          static_cast<int32_t>(timing::Transmitter::kAirTimeMs * 1000));

    // host::Pps has no jitter model at all: every edge board.h latched is
    // exact, so the whole interval histogram sits in the centre bucket.
    CHECK(statistics.pps_samples() > 0);
    CHECK(statistics.pps_bucket(3) == statistics.pps_samples());
    CHECK(statistics.holdover_events() == 0);
}

// The other half of the wiring: whatever already owns the PPS edge
// (boards/) is what the accumulator's holdover count depends on, and
// it has to fire on the transition, not on every pass spent unlocked.
TEST_CASE("rf: losing and regaining PPS through the simulator counts as holdover") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    // host::Pps has no jitter to show, but it does turn over exactly once per
    // simulated second, which is what the first sample needs.
    run_on(simulator, 0, 1200);
    REQUIRE(simulator.product().state().rf.timing_statistics.pps_samples() >= 1);

    simulator.world().set_pps_locked(false);
    run_on(simulator, 1205, 2000);
    simulator.world().set_pps_locked(true);
    run_on(simulator, 3210, 2000);

    const timing::SlotTimingStatistics& statistics =
        simulator.product().state().rf.timing_statistics;
    CHECK(statistics.holdover_events() >= 1);
    for (int bucket = 0; bucket < timing::SlotTimingStatistics::kBuckets; bucket++)
        if (bucket != 3) CHECK(statistics.pps_bucket(bucket) == 0);
}

TEST_CASE("rf: without an anchored clock nothing is transmitted") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    simulator.world().set_pps_locked(false);
    simulator.run(4000);
    CHECK(count_of(simulator.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(simulator.product().state().air.tx_ok == 0);
}

TEST_CASE("rf: on the ground the transmit rate drops to 0.1 Hz") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(0);
    run_on(simulator, past_settling(simulator), 8000);
    CHECK(count_of(simulator.world().air(), simulator::AirEvent::Tx) == 1);
}

// F5. A cold receiver's first solutions walk, and the flight state we derive
// from ground speed decides the transmit rate, so transmitting through that
// window publishes a track nobody flew. gnss::FirstFix has held the answer
// since it was written; until now nothing asked it.
TEST_CASE("rf: nothing goes on air while the receiver's solutions still walk") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);
    // Sixty metres of error at the first fix, decaying over half a minute.
    simulator.world().gnss().walk_amplitude_m = 60;
    simulator.world().gnss().walk_duration_ms = 30000;

    simulator.run(gnss::kFirstFixSettleMs - 2000);
    CHECK_FALSE(simulator.product().state().own.tx_settled);
    CHECK(count_of(simulator.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(simulator.product().state().air.tx_ok == 0);
    // Not because it has nothing to say: the fix is good and the clock anchored.
    CHECK(simulator.product().state().own.fix_valid);
    CHECK(simulator.product().state().clock.pps_locked);
    CHECK(simulator.product().state().own.prediction_residual_m >= gnss::kSettleResidualM);
    CHECK(simulator.product().ownship().first_fix().converged_fixes() == 0);

    // And there is one copy of that fact. Own-ship owns the window; the flag on
    // the bus is how the transmit gate reads it, and how anything else that
    // wants the first fix - the chirp on the panel, a status line over the link -
    // reads it too, rather than starting a second watch of its own.
    CHECK(simulator.product().ownship().first_fix().ever_fixed());
    CHECK_FALSE(simulator.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs - 2000));

    // The clock is a ceiling: a receiver that never steadies still goes on air.
    run_on(simulator, gnss::kFirstFixSettleMs - 2000, 4000);
    CHECK(simulator.product().state().own.tx_settled);
    CHECK(simulator.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs + 2000));
    CHECK(count_of(simulator.world().air(), simulator::AirEvent::Tx) > 0);
}

// F5. Three solutions the model predicted are the evidence the wait stood in for.
TEST_CASE("rf: a receiver the model predicts goes on air in seconds, not in twenty") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);

    uint32_t settled_at = 0;
    for (uint32_t now_ms = 0; now_ms <= gnss::kFirstFixSettleMs;
         now_ms += simulator::Simulator::kStepMs) {
        simulator.step(now_ms);
        if (settled_at == 0 && simulator.product().state().own.tx_settled) settled_at = now_ms;
    }
    REQUIRE(settled_at > 0);
    const uint32_t first_fix_ms = simulator.product().ownship().first_fix().fix_since_ms();
    MESSAGE("first fix at " << first_fix_ms << " ms, settled at " << settled_at << " ms");

    // Three residuals is the floor, and the twenty second ceiling is never reached.
    CHECK(settled_at - first_fix_ms >= gnss::kConvergedFixes * 1000);
    CHECK(settled_at - first_fix_ms < gnss::kFirstFixSettleMs / 2);
    CHECK(simulator.product().ownship().first_fix().converged_fixes() == gnss::kConvergedFixes);
    CHECK(simulator.product().state().own.prediction_residual_m < gnss::kSettleResidualM);
}

// F3. The burst leaves in the direct slot, 450 to 1000 ms into the second, and
// the ADS-L TimeStamp resolves to a quarter of a second. Encoding the fix's own
// second left every transmission claiming quarter zero - an instant 450 ms or
// more before the burst existed - while carrying a position from a third
// instant. This pins the pair together: the quarter the frame claims is the
// quarter it went on air in.
TEST_CASE("rf: the burst is dated when it leaves, and carries the position from then") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(90);
    simulator.world().set_track_deg(90);
    constexpr uint32_t kStepThatKeysOnTheInstantMs = 1;
    run_on(simulator, past_settling(simulator), 6000, kStepThatKeysOnTheInstantMs);

    const simulator::Air& air = simulator.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(record, frame));
        protocol::AdslPacket packet{};
        packet.reset();
        std::memcpy(packet.data, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(packet.check_crc() == 0);
        packet.descramble();
        REQUIRE(packet.address() == settings::address_of(platform::host::Identity::kSerial));

        // Inside the direct slot the quarter is 1, 2 or 3. Zero is the old bug.
        REQUIRE(timing::Scheduler::in_direct_slot(record.phase_ms));
        CHECK(int(packet.timestamp % 4) == record.phase_ms / 250);
        CHECK(int(packet.timestamp % 4) != 0);
        checked++;
    }
    CHECK(checked >= 4);
}

// F3's quality metric, through the whole pipeline: own-ship predicts the fix it
// last had forward to the instant the next one arrives and keeps the miss. A
// model that has stopped describing the aircraft is then a number on the bench
// rather than a surprise in the air (oss/nrf52-ogn-tracker src/ogn.h:1424-1430).
TEST_CASE("rf: the extrapolation residual is measured against the fix that arrives") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(90);
    simulator.world().set_track_deg(90);
    simulator.run(6000);
    CHECK(simulator.product().state().own.prediction_residual_valid);
    CHECK(simulator.product().state().own.prediction_residual_m <= 2);

    // A track that jumps 90 degrees between two solutions is a manoeuvre no
    // constant-turn model saw coming, and the residual says so on the solution
    // that closes the interval - and only on that one, because the model has the
    // aircraft again the moment it flies straight.
    simulator.world().set_track_deg(180);
    uint16_t worst_m = 0;
    for (uint32_t now_ms = 6000; now_ms <= 8000; now_ms += simulator::Simulator::kStepMs) {
        simulator.step(now_ms);
        const uint16_t resid_m = simulator.product().state().own.prediction_residual_m;
        if (resid_m > worst_m) worst_m = resid_m;
    }
    CHECK(worst_m > 10);
}

// M. The whole transmit chain stepped through the instant ports::Clock::milliseconds()
// turns over: the first-fix settling window, the fix-age gate, the once-a-second
// rate rule, the channel alternation and the rolling duty-cycle hour.
//
// The wrap is a value, not a wait: the clock starts 25 seconds short of it.
TEST_CASE("rf: own-ship keeps transmitting across the 49.7-day wrap") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(50);

    // Thirty seconds before the wrap: the receiver's configuration sequence, the
    // twenty-second settling window and a couple of seconds of transmitting all
    // happen before the counter turns over.
    uint32_t now_ms = 0u - 30000u;
    const uint32_t settled_at = now_ms + 28000u;
    while (now_ms != settled_at) {
        simulator.step(now_ms);
        now_ms += simulator::Simulator::kStepMs;
    }
    REQUIRE(simulator.product().state().own.tx_settled);
    const uint32_t sent_before = simulator.product().state().air.tx_ok;
    REQUIRE(sent_before > 0);
    simulator.world().air().clear();

    // Eight seconds, four of them on each side of zero.
    const uint32_t stop_at = now_ms + 8000u;
    while (now_ms != stop_at) {
        simulator.step(now_ms);
        now_ms += simulator::Simulator::kStepMs;
    }

    const simulator::Air& air = simulator.world().air();
    int transmissions = 0;
    uint32_t last_frequency = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        if (record.event != simulator::AirEvent::Tx) continue;
        transmissions++;
        // Still inside the direct slot, still alternating channel: the phase comes
        // off the PPS edge in microseconds(), which is 64-bit and does not wrap.
        CHECK(timing::Scheduler::in_direct_slot(record.phase_ms));
        CHECK(record.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kDirectEndMs);
        if (last_frequency != 0) CHECK(record.frequency_hz != last_frequency);
        last_frequency = record.frequency_hz;
    }
    // Eight seconds of flight is eight bursts, give or take the one the step
    // boundary lands on. A wrap that broke the rate rule would show up as zero.
    CHECK(transmissions >= 6);
    CHECK(simulator.product().state().air.tx_ok ==
          sent_before + static_cast<uint32_t>(transmissions));

    // And the hour that straddles the wrap is still an hour: the design rate is
    // half the band's allowance and the window did not forget what it holds.
    const timing::AirTime& air_time = simulator.product().radio().transmitter().air_time();
    CHECK(air_time.window_ms(now_ms) >=
          static_cast<uint32_t>(transmissions) * timing::Transmitter::kAirTimeMs);
    CHECK(simulator.product().radio().duty_permille(now_ms) < timing::AirTime::kLimitPermille);
    CHECK_FALSE(simulator.product().radio().over_budget());
}

// The ground rate is one burst per ten seconds, so most of this run is dwells
// that carry nothing, and none of them is a transmission that failed.
TEST_CASE("rf: a device on the ground transmits, and reports no burst it never armed") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(0);
    run_on(simulator, past_settling(simulator), 30000);

    CHECK(simulator.product().state().air.tx_ok > 0);
    CHECK(simulator.product().state().rf.timing_statistics.missed() == 0);

    const radio::Log& log = simulator.product().state().radio_log;
    int sent = 0, lost = 0;
    for (int i = 0; i < log.count(); i++) {
        if (log.newest(i).verdict == radio::Verdict::Transmitted) sent++;
        if (log.newest(i).verdict == radio::Verdict::Lost) lost++;
    }
    CHECK(sent > 0);
    CHECK(lost == 0);
}
