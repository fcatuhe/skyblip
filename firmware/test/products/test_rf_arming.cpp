// What is armed into the radio and when: a burst goes into the dwell already on air, a guard
// phase gets nothing, and a plan refused or held is counted as what it was.
#include <initializer_list>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

// The PLL word the driver writes back-converts a hertz or two short of the channel it asked for.
uint32_t tuned_khz(const models::Sx1262& chip) { return (chip.freq_hz + 500) / 1000; }

}  // namespace

// Slot 0's burst was added by a second arm at 450, read at 799, expired, and called the band busy.
TEST_CASE("rf: a plan armed mid-dwell waits for it, and an expired one is missed, not busy") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan flying{};
    flying.mode = ports::RfMode::RxMband;
    flying.freq_hz = timing::kMband0Hz;
    flying.start_us = 400000;
    flying.end_us = 799000;
    REQUIRE(rf.arm(flying) == Status::Ok);
    for (uint32_t t = 400; t <= 450; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    REQUIRE(tuned_khz(chip) == timing::kMband0Hz / 1000);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan queued = flying;
    queued.freq_hz = timing::kMband1Hz;
    queued.start_us = 455000;
    queued.tx = frame;
    queued.tx_len = sizeof(frame);
    queued.tx_at_us = 600000;
    REQUIRE(rf.arm(queued) == Status::Ok);

    for (uint32_t t = 460; t <= 800; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    // The dwell in flight kept the channel it was armed for, and the burst never went on air.
    CHECK(tuned_khz(chip) == timing::kMband0Hz / 1000);
    CHECK_FALSE(chip.tx_pending);

    int missed = 0;
    events::RfEvent e{};
    while (events.pop(e))
        if (e.type == events::RfEventType::Missed) missed++;
    CHECK(missed == 1);

    // The same queueing with nothing to transmit is a receive dwell that did not
    // happen, not a burst that was lost: the log would name it a failed
    // transmission and send a reader after a fault that is not there.
    ports::RfPlan next_dwell = flying;
    next_dwell.start_us = 1400000;
    next_dwell.end_us = 1799000;
    REQUIRE(rf.arm(next_dwell) == Status::Ok);
    for (uint32_t t = 1400; t <= 1450; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    ports::RfPlan listening = next_dwell;
    listening.start_us = 1455000;
    listening.end_us = 1600000;
    REQUIRE(rf.arm(listening) == Status::Ok);
    for (uint32_t t = 1460; t <= 1810; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    missed = 0;
    while (events.pop(e))
        if (e.type == events::RfEventType::Missed) missed++;
    CHECK(missed == 0);
}

namespace {

// The policy alone, with every plan it hands the executor kept for reading.
struct Armings {
    platform::host::Clock clock{};
    struct Recorder : ports::Rf {
        Status begin() override { return Status::Ok; }
        Status arm(const ports::RfPlan& plan) override {
            last = plan;
            arms++;
            return refuse ? Status::OutOfRange : Status::Ok;
        }
        void abort() override {}
        ports::RfPlan last{};
        uint32_t arms{0};
        bool refuse{false};
    } rf{};
    bus::Bus bus{};
    bus::State state{};
    ports::NullRoles null{};
    ports::Roles roles{clock,
                       rf,
                       null.link,
                       null.display,
                       null.kv,
                       null.log_flash,
                       null.annunciator,
                       null.dfu,
                       null.die_temperature,
                       null.indicator,
                       null.gnss,
                       ports::Capability::Rf,
                       0x5B7E57};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::RadioService radio{context, settings};

    static constexpr uint64_t kEdgeUs = 30000000;

    void tick_at(int phase_ms) { tick_in(0, phase_ms); }

    void tick_in(uint32_t second, int phase_ms) {
        const uint64_t edge_us = kEdgeUs + static_cast<uint64_t>(second) * 1000000;
        const uint64_t now_us = edge_us + static_cast<uint64_t>(phase_ms) * 1000;
        clock.set_micros(now_us);
        state.clock.pps_locked = true;
        state.clock.utc_valid = true;
        state.clock.pps_edge_us = edge_us;
        radio.tick(static_cast<uint32_t>(now_us / 1000));
    }

    void ready_to_transmit(uint32_t utc) {
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.own.utc = utc;
        state.own.fix_ms = static_cast<uint32_t>(kEdgeUs / 1000);
    }
};

}  // namespace

// The guard phases between dwells report the dwell that has just closed, and a
// window already behind the phase used to be armed as a 1 ms stub the executor
// then dropped: one fabricated LOST per pass that landed in a guard.
TEST_CASE("rf: the guard between two dwells is not a dwell, and nothing is armed inside it") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);

    a.tick_at(100);
    const uint32_t armed_in_slot1 = a.rf.arms;
    CHECK(a.rf.last.freq_hz == timing::kMband1Hz);

    a.tick_at(397);
    CHECK(a.rf.arms == armed_in_slot1);

    a.tick_at(799);
    CHECK(a.rf.arms == armed_in_slot1);

    // And the dwell that opens right after the guard is armed whole.
    a.tick_at(400);
    CHECK(a.rf.arms == armed_in_slot1 + 1);
    CHECK(a.rf.last.freq_hz == timing::kMband0Hz);
    CHECK(a.rf.last.end_us - a.rf.last.start_us > 300000);
}

// Whether a burst may go out is decided on the fix, the rate and the slot, and
// those clear when they clear: a solution that lands after the dwell opened used
// to cost the whole second, because the dwell was armed at its edge and never
// looked at again.
TEST_CASE("rf: a burst whose gates clear inside the dwell is armed into that dwell") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    a.state.own.utc_valid = true;
    a.state.own.tx_settled = true;
    a.state.own.flight_state = 2;
    a.state.own.fix_ms = static_cast<uint32_t>(Armings::kEdgeUs / 1000);

    a.tick_at(400);
    const uint32_t armed_at_the_edge = a.rf.arms;
    CHECK(a.rf.last.tx == nullptr);

    a.state.own.fix_valid = true;
    a.tick_at(410);
    CHECK(a.rf.arms == armed_at_the_edge + 1);
    REQUIRE(a.rf.last.tx != nullptr);
    CHECK(a.rf.last.tx_at_us >= a.rf.last.start_us);
    CHECK(a.rf.last.tx_at_us < a.rf.last.end_us);

    for (int phase = 420; phase < 790; phase += 10) a.tick_at(phase);
    CHECK(a.rf.arms == armed_at_the_edge + 1);
}

// The dwell is armed at its edge, where the gates on a burst may not have
// cleared yet; the burst is armed when it is due, into the dwell already on air.
// A second plan for the same channel inside the same window is that burst, not
// the next dwell, and queueing it behind the one flying meant it never keyed.
TEST_CASE("rf: a burst armed for the dwell in flight goes out in it") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan dwell{};
    dwell.mode = ports::RfMode::RxMband;
    dwell.freq_hz = timing::kMband0Hz;
    dwell.start_us = 400000;
    dwell.end_us = 799000;
    REQUIRE(rf.arm(dwell) == Status::Ok);
    for (uint32_t t = 400; t <= 450; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan burst = dwell;
    burst.start_us = 455000;
    burst.tx = frame;
    burst.tx_len = sizeof(frame);
    burst.tx_at_us = 600000;
    REQUIRE(rf.arm(burst) == Status::Ok);

    uint8_t on_air[events::kRfEventBytes];
    uint8_t on_air_len = 0;
    for (uint32_t t = 460; t <= 800; t += 10) {
        clock.set_millis(t);
        rf.service(t);
        if (chip.take_tx(on_air, on_air_len)) chip.signal_tx_done();
    }

    CHECK(on_air_len > 0);
    CHECK(tuned_khz(chip) == timing::kMband0Hz / 1000);
    int sent = 0, missed = 0;
    events::RfEvent e{};
    while (events.pop(e)) {
        if (e.type == events::RfEventType::TxDone) sent++;
        if (e.type == events::RfEventType::Missed) missed++;
    }
    CHECK(sent == 1);
    CHECK(missed == 0);
}

// A burst refused before it was ever armed left the counters moving and the page
// silent, which reads exactly like a dead transmitter to the one person looking.
TEST_CASE("rf: a plan the radio refuses is a burst that never armed, not one that went missing") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    a.ready_to_transmit(1000);
    a.rf.refuse = true;

    a.tick_at(400);

    REQUIRE(a.rf.last.tx != nullptr);
    REQUIRE(a.state.radio_log.count() == 1);
    CHECK(a.state.radio_log.newest(0).event == radio::Event::Unarmed);
    CHECK(a.state.rf.timing_stats.missed() == 1);
}

// The hour's allowance holds every burst until it frees up again, so one row says
// when that began: a spell of them would push the sky itself off a 16-row tape.
TEST_CASE("rf: the hour's air-time budget holding a burst is said once, and counted every time") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    a.ready_to_transmit(1000);

    // EN 300 220-2 band M is 10 permille of the hour: 7200 bursts of 5 ms fill it.
    for (int i = 0; i < 7202; i++) {
        a.state.air.tx_ok++;
        a.tick_at(100);
    }
    REQUIRE(a.state.radio_log.count() == 0);

    a.state.own.utc += 2;  // the lower channel's second again
    a.tick_at(400);

    REQUIRE(a.state.radio_log.count() == 1);
    CHECK(a.state.radio_log.newest(0).event == radio::Event::Held);
    CHECK(a.state.radio_log.newest(0).band == model::Band::M);
    CHECK(a.state.rf.timing_stats.refused() == 1);

    a.state.own.utc += 2;
    a.state.own.fix_ms = static_cast<uint32_t>((Armings::kEdgeUs + 1000000) / 1000);
    a.tick_in(1, 205);
    a.tick_in(1, 400);

    CHECK(a.state.radio_log.count() == 1);
    CHECK(a.state.rf.timing_stats.refused() == 2);
}
