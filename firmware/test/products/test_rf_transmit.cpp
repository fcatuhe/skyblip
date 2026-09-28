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
#include "core/timing/timing_stats.h"
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

Spent spent_so_far(simulator::Simulator& h) {
    return {h.product().state().air.tx_ok, h.product().radio().transmitter().air_time().total_ms()};
}

}  // namespace

TEST_CASE("rf: own-ship transmits once a second, inside its window, alternating channel") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(h);
    const Spent before = spent_so_far(h);
    run_on(h, from_ms, 6000);

    const simulator::Air& air = h.world().air();
    int transmissions = 0;
    uint32_t last_freq = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        transmissions++;
        // Always inside the direct slot, even though the dwell that carries the
        // burst runs 200 ms past it.
        CHECK(timing::Scheduler::in_direct_slot(r.phase_ms));
        CHECK(r.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kDirectEnd);
        if (last_freq != 0) CHECK(r.freq_hz != last_freq);
        last_freq = r.freq_hz;
    }
    // Six seconds of flight, one burst a second, less the one the cleared tape cut in half.
    CHECK(transmissions >= 5);
    CHECK(h.product().state().air.tx_ok - before.tx_ok == static_cast<uint32_t>(transmissions));

    // E1 and E2, read off the service that spent them: the floor the carrier
    // sense threshold is derived from, and every millisecond that went on air.
    CHECK(h.product().radio().noise_floor().samples() > 0);
    CHECK(h.product().radio().noise_floor().dbm() < timing::NoiseFloor::kSeedDbm);
    CHECK(h.product().radio().transmitter().air_time().total_ms() - before.air_time_ms ==
          static_cast<uint32_t>(transmissions) * timing::Transmitter::kAirTimeMs);
    CHECK(h.product().radio().duty_permille(from_ms + 6000) < timing::AirTime::kLimitPermille);
    CHECK_FALSE(h.product().radio().over_budget());
}

// §C.5 reserves 0..200 and this is the one burst that goes there: core/timing/README.md.
TEST_CASE("rf: the callsign goes out in slot 1's tail, once every ten seconds, on channel 1") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    std::strncpy(h.product().settings().callsign, "D-KXYZ", go::kCallsignCap - 1);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    const uint32_t from_ms = past_settling(h);
    run_on(h, from_ms, 21000);

    const simulator::Air& air = h.world().air();
    int named = 0;
    int positions = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        if (timing::Scheduler::in_direct_slot(r.phase_ms)) {
            positions++;
            continue;
        }
        named++;
        CHECK(r.phase_ms < timing::kSlot1Wrap);
        CHECK(r.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kCallsignEnd - 1000);
        CHECK(simulator::Air::tuned_to(r.freq_hz, timing::kMband1Hz));
    }
    // Twenty-one seconds of flight: twenty-one positions and two names.
    CHECK(positions >= 19);
    CHECK(named == 2);

    // And the tape says which burst was which, where a position prints its rate.
    const radio::Log& log = h.product().state().radio_log;
    int rows = 0;
    for (int i = 0; i < log.count(); i++)
        if (log.newest(i).event == radio::Event::Transmitted && log.newest(i).callsign) rows++;
    CHECK(rows > 0);
}

// The default is a device nobody has named, and a nameless burst would say nothing.
TEST_CASE("rf: a device with no callsign set transmits nothing in the reserved window") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    REQUIRE(h.product().settings().callsign[0] == 0);
    run_on(h, past_settling(h), 21000);

    const simulator::Air& air = h.world().air();
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        CHECK(timing::Scheduler::in_direct_slot(r.phase_ms));
    }
}

TEST_CASE("rf: what own-ship put on air decodes back to own-ship state") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().set_altitude_m(1200);
    run_on(h, past_settling(h), 3000);

    const simulator::Air& air = h.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        // Own bursts go on air as chips behind the ADS-L sync word, so reading
        // them back means framing them the way the receiver does.
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(r, frame));
        REQUIRE(frame.system == protocol::System::AdslDirect);
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        CHECK(p.address() == h.platform().device_addr());
        CHECK(p.alt_m() == to_metres(Millimetres(h.product().state().own.alt_mm)).v);
        CHECK(p.FlightState == 2);
        checked++;
    }
    CHECK(checked > 0);
}

// G6's wiring, not the bucket arithmetic (core/test_timing.cpp already pins
// that down): RadioService owns the deadline, TrafficService the executor's
// report, and this proves the two actually meet in state.rf.timing_stats rather
// than each keeping a private opinion.
TEST_CASE("rf: a completed burst lands in the bench's dwell-phase histogram") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    // 1 ms passes: a 5 ms one spends the burst's completion slack before the radio is serviced.
    run_on(h, past_settling(h), 6000, 1);

    const timing::SlotTimingStats& stats = h.product().state().rf.timing_stats;
    CHECK(stats.dwell_samples() > 0);
    CHECK(stats.missed() == 0);
    CHECK(stats.refused() == 0);
    // Never before the instant it was armed for, and never as late as the burst is long.
    CHECK(stats.dwell_worst_us() >= 0);
    CHECK(stats.dwell_worst_us() < static_cast<int32_t>(timing::Transmitter::kAirTimeMs * 1000));

    // host::Pps has no jitter model at all: every edge board.h latched is
    // exact, so the whole interval histogram sits in the centre bucket.
    CHECK(stats.pps_samples() > 0);
    CHECK(stats.pps_bucket(3) == stats.pps_samples());
    CHECK(stats.holdover_events() == 0);
}

// The other half of the wiring: whatever already owns the PPS edge
// (boards/) is what the accumulator's holdover count depends on, and
// it has to fire on the transition, not on every pass spent unlocked.
TEST_CASE("rf: losing and regaining PPS through the simulator counts as holdover") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    // host::Pps has no jitter to show, but it does turn over exactly once per
    // simulated second, which is what the first sample needs.
    run_on(h, 0, 1200);
    REQUIRE(h.product().state().rf.timing_stats.pps_samples() >= 1);

    h.world().set_pps_locked(false);
    run_on(h, 1205, 2000);
    h.world().set_pps_locked(true);
    run_on(h, 3210, 2000);

    const timing::SlotTimingStats& stats = h.product().state().rf.timing_stats;
    CHECK(stats.holdover_events() >= 1);
    for (int b = 0; b < timing::SlotTimingStats::kBuckets; b++)
        if (b != 3) CHECK(stats.pps_bucket(b) == 0);
}

TEST_CASE("rf: without an anchored clock nothing is transmitted") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().set_pps_locked(false);
    h.run(4000);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(h.product().state().air.tx_ok == 0);
}

TEST_CASE("rf: on the ground the transmit rate drops to 0.1 Hz") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    run_on(h, past_settling(h), 8000);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 1);
}

// F5. A cold receiver's first solutions walk, and the flight state we derive
// from ground speed decides the transmit rate, so transmitting through that
// window publishes a track nobody flew. gnss::FirstFix has held the answer
// since it was written; until now nothing asked it.
TEST_CASE("rf: nothing goes on air while the receiver's solutions still walk") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    // Sixty metres of error at the first fix, decaying over half a minute.
    h.world().gnss().walk_m = 60;
    h.world().gnss().walk_ms = 30000;

    h.run(gnss::kFirstFixSettleMs - 2000);
    CHECK_FALSE(h.product().state().own.tx_settled);
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) == 0);
    CHECK(h.product().state().air.tx_ok == 0);
    // Not because it has nothing to say: the fix is good and the clock anchored.
    CHECK(h.product().state().own.fix_valid);
    CHECK(h.product().state().clock.pps_locked);
    CHECK(h.product().state().own.pred_resid_m >= gnss::kSettleResidualM);
    CHECK(h.product().ownship().first_fix().converged_fixes() == 0);

    // And there is one copy of that fact. Own-ship owns the window; the flag on
    // the bus is how the transmit gate reads it, and how anything else that
    // wants the first fix - the chirp on the panel, a status line over the link -
    // reads it too, rather than starting a second watch of its own.
    CHECK(h.product().ownship().first_fix().ever_fixed());
    CHECK_FALSE(h.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs - 2000));

    // The clock is a ceiling: a receiver that never steadies still goes on air.
    run_on(h, gnss::kFirstFixSettleMs - 2000, 4000);
    CHECK(h.product().state().own.tx_settled);
    CHECK(h.product().ownship().first_fix().settled(gnss::kFirstFixSettleMs + 2000));
    CHECK(count_of(h.world().air(), simulator::AirEvent::Tx) > 0);
}

// F5. Three solutions the model predicted are the evidence the wait stood in for.
TEST_CASE("rf: a receiver the model predicts goes on air in seconds, not in twenty") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);

    uint32_t settled_at = 0;
    for (uint32_t t = 0; t <= gnss::kFirstFixSettleMs; t += simulator::Simulator::kStepMs) {
        h.step(t);
        if (settled_at == 0 && h.product().state().own.tx_settled) settled_at = t;
    }
    REQUIRE(settled_at > 0);
    const uint32_t first_fix_ms = h.product().ownship().first_fix().fix_since_ms();
    MESSAGE("first fix at " << first_fix_ms << " ms, settled at " << settled_at << " ms");

    // Three residuals is the floor, and the twenty second ceiling is never reached.
    CHECK(settled_at - first_fix_ms >= gnss::kConvergedFixes * 1000);
    CHECK(settled_at - first_fix_ms < gnss::kFirstFixSettleMs / 2);
    CHECK(h.product().ownship().first_fix().converged_fixes() == gnss::kConvergedFixes);
    CHECK(h.product().state().own.pred_resid_m < gnss::kSettleResidualM);
}

// F3. The burst leaves in the direct slot, 450 to 1000 ms into the second, and
// the ADS-L TimeStamp resolves to a quarter of a second. Encoding the fix's own
// second left every transmission claiming quarter zero - an instant 450 ms or
// more before the burst existed - while carrying a position from a third
// instant. This pins the pair together: the quarter the frame claims is the
// quarter it went on air in.
TEST_CASE("rf: the burst is dated when it leaves, and carries the position from then") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(90);
    h.world().set_track_deg(90);
    constexpr uint32_t kStepThatKeysOnTheInstantMs = 1;
    run_on(h, past_settling(h), 6000, kStepThatKeysOnTheInstantMs);

    const simulator::Air& air = h.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(r, frame));
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        REQUIRE(p.address() == h.platform().device_addr());

        // Inside the direct slot the quarter is 1, 2 or 3. Zero is the old bug.
        REQUIRE(timing::Scheduler::in_direct_slot(r.phase_ms));
        CHECK(int(p.TimeStamp % 4) == r.phase_ms / 250);
        CHECK(int(p.TimeStamp % 4) != 0);
        checked++;
    }
    CHECK(checked >= 4);
}

// F3's quality metric, through the whole pipeline: own-ship predicts the fix it
// last had forward to the instant the next one arrives and keeps the miss. A
// model that has stopped describing the aircraft is then a number on the bench
// rather than a surprise in the air (oss/nrf52-ogn-tracker src/ogn.h:1424-1430).
TEST_CASE("rf: the extrapolation residual is measured against the fix that arrives") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(90);
    h.world().set_track_deg(90);
    h.run(6000);
    CHECK(h.product().state().own.pred_resid_valid);
    CHECK(h.product().state().own.pred_resid_m <= 2);

    // A track that jumps 90 degrees between two solutions is a manoeuvre no
    // constant-turn model saw coming, and the residual says so on the solution
    // that closes the interval - and only on that one, because the model has the
    // aircraft again the moment it flies straight.
    h.world().set_track_deg(180);
    uint16_t worst_m = 0;
    for (uint32_t t = 6000; t <= 8000; t += simulator::Simulator::kStepMs) {
        h.step(t);
        const uint16_t resid_m = h.product().state().own.pred_resid_m;
        if (resid_m > worst_m) worst_m = resid_m;
    }
    CHECK(worst_m > 10);
}

// M. The whole transmit chain stepped through the instant ports::Clock::millis()
// turns over: the first-fix settling window, the fix-age gate, the once-a-second
// rate rule, the channel alternation and the rolling duty-cycle hour.
//
// The wrap is a value, not a wait: the clock starts 25 seconds short of it.
TEST_CASE("rf: own-ship keeps transmitting across the 49.7-day wrap") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);

    // Thirty seconds before the wrap: the receiver's configuration sequence, the
    // twenty-second settling window and a couple of seconds of transmitting all
    // happen before the counter turns over.
    uint32_t t = 0u - 30000u;
    const uint32_t settled_at = t + 28000u;
    while (t != settled_at) {
        h.step(t);
        t += simulator::Simulator::kStepMs;
    }
    REQUIRE(h.product().state().own.tx_settled);
    const uint32_t sent_before = h.product().state().air.tx_ok;
    REQUIRE(sent_before > 0);
    h.world().air().clear();

    // Eight seconds, four of them on each side of zero.
    const uint32_t stop_at = t + 8000u;
    while (t != stop_at) {
        h.step(t);
        t += simulator::Simulator::kStepMs;
    }

    const simulator::Air& air = h.world().air();
    int transmissions = 0;
    uint32_t last_freq = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        transmissions++;
        // Still inside the direct slot, still alternating channel: the phase comes
        // off the PPS edge in micros(), which is 64-bit and does not wrap.
        CHECK(timing::Scheduler::in_direct_slot(r.phase_ms));
        CHECK(r.phase_ms + timing::Transmitter::kAirTimeMs <= timing::kDirectEnd);
        if (last_freq != 0) CHECK(r.freq_hz != last_freq);
        last_freq = r.freq_hz;
    }
    // Eight seconds of flight is eight bursts, give or take the one the step
    // boundary lands on. A wrap that broke the rate rule would show up as zero.
    CHECK(transmissions >= 6);
    CHECK(h.product().state().air.tx_ok == sent_before + static_cast<uint32_t>(transmissions));

    // And the hour that straddles the wrap is still an hour: the design rate is
    // half the band's allowance and the window did not forget what it holds.
    const timing::AirTime& air_time = h.product().radio().transmitter().air_time();
    CHECK(air_time.window_ms(t) >=
          static_cast<uint32_t>(transmissions) * timing::Transmitter::kAirTimeMs);
    CHECK(h.product().radio().duty_permille(t) < timing::AirTime::kLimitPermille);
    CHECK_FALSE(h.product().radio().over_budget());
}

// The ground rate is one burst per ten seconds, so most of this run is dwells
// that carry nothing, and none of them is a transmission that failed.
TEST_CASE("rf: a device on the ground transmits, and reports no burst it never armed") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    run_on(h, past_settling(h), 30000);

    CHECK(h.product().state().air.tx_ok > 0);
    CHECK(h.product().state().rf.timing_stats.missed() == 0);

    const radio::Log& log = h.product().state().radio_log;
    int sent = 0, lost = 0;
    for (int i = 0; i < log.count(); i++) {
        if (log.newest(i).event == radio::Event::Transmitted) sent++;
        if (log.newest(i).event == radio::Event::Lost) lost++;
    }
    CHECK(sent > 0);
    CHECK(lost == 0);
}
