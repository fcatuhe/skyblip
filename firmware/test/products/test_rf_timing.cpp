// What is on the 868 MHz air, and when. Every burst in these tests is a real
// scrambled ADS-L frame put on the channel at an absolute instant. The radio
// hears it only if the firmware had the receiver tuned there at that moment.
// A slot-map regression shows up as DEAF records, not as a missing feature.
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

using namespace skyblip;

namespace {

// A receiver-only world: PPS unlocked keeps own-ship off air, so the only bursts are a neighbour's.
void listen_at(simulator::Simulator& h, int phase_ms, int slot) {
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    h.world().add_aircraft(800, 0, 0, 30, 180, phase_ms, slot);
}

// F5: own-ship holds every burst until the receiver's first solutions have
// settled, so a transmit test that starts the clock at zero is a test of the
// settling window and nothing else. This is the far side of it, with the tape
// wiped so the bursts that follow are the ones under test.
uint32_t past_settling(simulator::Simulator& h) {
    h.run(gnss::kFirstFixSettleMs);
    h.world().air().clear();
    return gnss::kFirstFixSettleMs;
}

// What own-ship had already spent when a case starts measuring.
struct Spent {
    uint32_t tx_ok{0};
    uint32_t air_time_ms{0};
};

Spent spent_so_far(simulator::Simulator& h) {
    return {h.product().state().air.tx_ok, h.product().radio().transmitter().air_time().total_ms()};
}

void run_on(simulator::Simulator& h, uint32_t from_ms, uint32_t for_ms,
            uint32_t step_ms = simulator::Simulator::kStepMs) {
    for (uint32_t t = from_ms; t <= from_ms + for_ms; t += step_ms) h.step(t);
}

int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int n = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == want) n++;
    return n;
}

// A device on the next bench: the real driver over its own part, armed for the M-band dwell.
struct Peer {
    models::Sx1262 chip;
    parts::Sx1262 radio{chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};

    Peer() {
        REQUIRE(radio.begin() == Status::Ok);
        parts::RadioConfig cfg{};
        cfg.sync = protocol::kSharedSync;
        cfg.sync_bits = protocol::kSharedSyncBits;
        cfg.payload_bytes = protocol::kRxChipBytes;
        cfg.bitrate = protocol::kMbandChipRateBps;
        cfg.fdev_hz = protocol::kMbandDeviationHz;
        cfg.bandwidth_hz = protocol::kMbandChannelBandwidthHz;
        REQUIRE(radio.configure_radio(cfg) == Status::Ok);
        REQUIRE(radio.start_receive() == Status::Ok);
    }

    bool frames(const simulator::AirRecord& burst, protocol::Frame& out) {
        if (!chip.receive_air(burst.chips, burst.len, /*crc_error=*/false, burst.rssi_dbm,
                              burst.bitrate))
            return false;
        uint8_t buf[events::kRfEventBytes];
        const parts::RadioEvent ev = radio.poll(buf, sizeof(buf));
        REQUIRE(radio.start_receive() == Status::Ok);
        if (ev.type != parts::RadioEventType::RxDone) return false;
        return protocol::receive_mband(buf, ev.len, out);
    }
};

}  // namespace

// A quiet band still frames a few bursts a minute, and the tape is sixteen rows.
TEST_CASE("rf: a window the band's own noise walked through is counted, never put on the tape") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    const uint32_t start = past_settling(h);
    run_on(h, start, 1000);

    uint8_t burst[protocol::kSyncWindowChipBytes + protocol::kRxChipBytes];
    std::memcpy(burst, protocol::kSharedSync, protocol::kSyncWindowChipBytes);
    models::RfChannel noise(0xC0FFEE);
    for (size_t i = protocol::kSyncWindowChipBytes; i < sizeof(burst); i++)
        burst[i] = static_cast<uint8_t>(noise.next());

    const int rows_before = h.product().state().radio_log.count();
    const uint32_t bad_before = h.product().state().air.rx_bad;
    const uint32_t at_ms = start + 2000 + timing::kSlot0Start + 200;
    h.step(at_ms);
    REQUIRE(h.platform().chips().radio.receive_air(burst, sizeof(burst), /*crc_error=*/false,
                                                   /*rssi=*/-112, protocol::kMbandChipRateBps));
    run_on(h, at_ms, 200);

    CHECK(h.product().state().air.rx_noise == 1);
    CHECK(h.product().state().air.rx_bad == bad_before);
    CHECK(h.product().state().radio_log.count() == rows_before);
}

// The page a bench reads instead of guessing from two counters that only ever climb.
TEST_CASE("rf: what happened on air reaches the station log, sent and heard alike") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const radio::Log& log = h.product().state().radio_log;
    REQUIRE(log.count() > 0);
    bool sent = false, heard = false;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.event == radio::Event::Transmitted) sent = true;
        if (entry.event != radio::Event::Received) continue;
        heard = true;
        CHECK(entry.addr != 0);
        CHECK(entry.source == model::Source::AdslDirect);
        CHECK(entry.rssi_valid);
        CHECK(entry.utc);
    }
    CHECK(sent);
    CHECK(heard);
}

// One burst sent on one device and heard on another is compared on this phase alone.
TEST_CASE("rf: the log dates a burst to the millisecond of the second it landed in") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const radio::Log& log = h.product().state().radio_log;
    int dated = 0, channelled = 0;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        REQUIRE(entry.phase_valid);
        CHECK(entry.into_ms < 1000);
        // The phase is where the burst ENDED, so only a dwell it cannot have run into names one.
        if (entry.band == model::Band::M && entry.into_ms < timing::kSlot0End - 100) {
            CHECK(entry.channel == 0);
            channelled++;
        }
        if (entry.event != radio::Event::Transmitted) continue;
        dated++;
        CHECK(entry.into_ms >= timing::kDirectStart);
        CHECK(entry.tx_span_valid);
        CHECK(entry.tx_span_us > 0);
    }
    CHECK(dated > 0);
    CHECK(channelled > 0);
}

// Two devices on a bench, both transmitting, neither ever hearing the other.
TEST_CASE("rf: a burst own-ship put on air is one another skyBlip frames") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    run_on(h, past_settling(h), 6000);

    const simulator::Air& air = h.world().air();
    Peer peer;
    int framed = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& mine = air.record(i);
        if (mine.event != simulator::AirEvent::Tx) continue;
        protocol::Frame heard{};
        REQUIRE(peer.frames(mine, heard));
        CHECK(heard.system == protocol::System::AdslDirect);
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, heard.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        CHECK(p.address() == h.platform().device_addr());
        framed++;
    }
    CHECK(framed > 0);
}

TEST_CASE("rf: a paraglider below the flight speed transmits every second, flight undefined") {
    for (const uint8_t type : {uint8_t{7}, uint8_t{1}}) {
        CAPTURE(int(type));
        simulator::Simulator h;
        REQUIRE(h.setup() == Status::Ok);
        h.product().settings().aircraft_type = type;
        h.world().set_fix(true);
        h.world().set_speed_kt(15);
        run_on(h, past_settling(h), 20000);

        const simulator::Air& air = h.world().air();
        Peer peer;
        int positions = 0;
        for (int i = 0; i < air.record_count(); i++) {
            const simulator::AirRecord& mine = air.record(i);
            if (mine.event != simulator::AirEvent::Tx) continue;
            protocol::Frame heard{};
            REQUIRE(peer.frames(mine, heard));
            protocol::AdslPacket p{};
            p.init();
            std::memcpy(&p.Version, heard.data, protocol::kAdslFrameBytes);
            p.descramble();
            if (!p.is_position()) continue;
            positions++;
            CHECK(p.FlightState == (type == 7 ? 0 : 1));
        }
        if (type == 7)
            CHECK(positions >= 19);
        else
            CHECK(positions <= 3);
    }
}

TEST_CASE("rf: a burst is heard only inside the dwell that owns its channel") {
    struct Case {
        int phase_ms;
        int slot;
        bool heard;
    };
    // The O-band dwell runs 205..395 (framed on our ground station's burst), the
    // M-band dwells 400..800 on 868.2 and 800..1000 on 868.4. 400..450 is the
    // slice we listen to but do not transmit in: FLARM-generation traffic lives
    // there.
    const Case cases[] = {
        {50, 0, false},
        {150, 0, false},
        {300, 0, false},
        {390, 0, false},
        {420, 0, true},
        {470, 0, true},
        {600, 0, true},
        {780, 0, true},
        {820, 1, true},
        {900, 1, true},
        {990, 1, true},
        // The second dwell reaches 1200 ms: its tail is heard on 868.4, not
        // deaf. Nothing of ours transmits there, everyone else still may.
        {100, 1, true},
        {190, 1, true},
        // Right time, wrong channel: a slot-1 burst still on 868.2 is a burst
        // we cannot hear, which is why the channel is part of the dwell.
        {900, 0, false},
        // Right channel, wrong time: 868.4 during slot 0.
        {600, 1, false},
    };
    for (const Case& c : cases) {
        simulator::Simulator h;
        listen_at(h, c.phase_ms, c.slot);
        h.run(4000);
        const int heard = count_of(h.world().air(), simulator::AirEvent::Rx);
        const int deaf = count_of(h.world().air(), simulator::AirEvent::Deaf);
        CAPTURE(c.phase_ms);
        CAPTURE(c.slot);
        CHECK((heard > 0) == c.heard);
        CHECK((deaf > 0) == !c.heard);
        CHECK((h.product().state().air.rx_ok > 0u) == c.heard);
    }
}

// A burst near the end of the second finishes after the second has rolled over.
// Its phase is its own instant's, not the instant we noticed it ended.
TEST_CASE("rf: a burst that straddles the second is dated by when it started") {
    simulator::Simulator h;
    listen_at(h, 995, 1);
    h.run(4000);
    const simulator::Air& air = h.world().air();
    REQUIRE(air.record_count() > 0);
    for (int i = 0; i < air.record_count(); i++) CHECK(air.record(i).phase_ms == 995);
}

TEST_CASE("rf: a heard burst is the frame that was on air, decoded by the real path") {
    simulator::Simulator h;
    listen_at(h, 600, 0);
    h.run(3000);
    REQUIRE(h.product().state().air.rx_ok > 0);
    REQUIRE(h.product().state().traffic.count() == 1);
    char line[160];
    REQUIRE(h.world().air().format(0, line, sizeof(line)) > 0);
    // The tape decodes what the receiver decoded: same address, CRC intact.
    CHECK(std::string(line).find("300001") != std::string::npos);
    CHECK(std::string(line).find("crc ok") != std::string::npos);
    CHECK(std::string(line).find("868.200") != std::string::npos);
}

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
    // Never before the instant it was armed for, and never past the dwell that carried it.
    CHECK(stats.dwell_worst_us() >= 0);
    CHECK(stats.dwell_worst_us() < timing::kSlot0End * 1000);

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
    run_on(h, past_settling(h), 6000);

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

// --- J. Transmit loopback ----------------------------------------------------
// SoftRF suppresses a transmission whose buffer equals the last frame received
// and reports "$PSRFE,RF loopback is detected on Tx" (src/driver/RF.cpp:381-396).
// That guard exists because it happened in the field, and the shape of its
// firmware is why: one RF driver owns a shared TxBuffer/RxBuffer pair, a received
// frame is parsed out of the same memory a transmission is composed into, and
// its relay and bridge paths do put received traffic back on air.
//
// Ours cannot reach that state, and this is the case that says so rather than a
// paragraph claiming it. The transmit buffer (RadioService::outgoing_) is only
// ever written by protocol::from_own, whose inputs are own-ship state, the device
// address and the settings; a received frame's only path is the RfEvent queue
// into TrafficService and the traffic table, which nothing transmits from. There
// is no relay feature, no repeater and no second writer. So there is no guard in
// the driver: a guard against an impossible fault is a test nobody can fail
// honestly and a comparison in the one place a dwell cannot afford one.
//
// What we do instead is assert the property the guard would protect, over the
// real air, with both directions live in the same second.
TEST_CASE("rf: nothing own-ship transmits is a frame own-ship received") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(50);
    // A neighbour transmitting inside the first M-band dwell, so every second
    // carries a reception and a transmission on the same radio.
    h.world().add_aircraft(1200, 300, 50, 30, 90, 600, 0);
    run_on(h, past_settling(h), 6000);

    const simulator::Air& air = h.world().air();
    int received = 0, transmitted = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& mine = air.record(i);
        if (mine.event != simulator::AirEvent::Tx) continue;
        transmitted++;
        protocol::Frame sent{};
        REQUIRE(simulator::Air::framed(mine, sent));
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, sent.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        // Every burst we put on air is ours, by address: a relayed frame would
        // carry the neighbour's.
        CHECK(p.address() == h.platform().device_addr());

        for (int j = 0; j < air.record_count(); j++) {
            const simulator::AirRecord& heard = air.record(j);
            if (heard.event != simulator::AirEvent::Rx) continue;
            const bool identical =
                heard.len == mine.len && std::memcmp(heard.chips, mine.chips, mine.len) == 0;
            CHECK_FALSE(identical);
        }
    }
    for (int i = 0; i < air.record_count(); i++)
        if (air.record(i).event == simulator::AirEvent::Rx) received++;
    // Neither half may be zero, or the case above proves nothing.
    CHECK(received > 0);
    CHECK(transmitted > 0);
    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().air.tx_ok > 0);
}

// --- J. Range sanity, end to end --------------------------------------------
// The gate on the traffic table's door (core/traffic/sanity.h) with the whole
// path in front of it: a real frame, on the real air, through the model's sync
// detector, the driver, the executor and the decoder. A frame that survives all
// of that and still claims to be 120 km away did not arrive from there.
TEST_CASE("rf: a decoded burst claiming an impossible range never reaches the radar") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);  // receive only, so the tape is the neighbour's
    h.world().add_aircraft(120000, 0, 0, 30, 180, 600, 0);
    h.run(4000);

    // Heard, decoded, CRC intact: the frame is not being refused by the radio or
    // by the protocol layer.
    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().air.rx_bad == 0);
    // And refused by the table, counted, with nothing on the screen.
    CHECK(h.product().state().traffic.count() == 0);
    CHECK(h.product().state().traffic.implausible_count() > 0);
    CHECK(h.product().state().alarm_level == traffic::Level::None);
}

// The same air with the same aircraft at a range this radio can actually reach:
// the gate is a ceiling on nonsense, not a filter on traffic.
TEST_CASE("rf: a burst from a range the link budget allows is traffic as before") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_pps_locked(false);
    h.world().add_aircraft(8000, 0, 0, 30, 180, 600, 0);
    h.run(4000);

    CHECK(h.product().state().air.rx_ok > 0);
    CHECK(h.product().state().traffic.count() == 1);
    CHECK(h.product().state().traffic.implausible_count() == 0);
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
