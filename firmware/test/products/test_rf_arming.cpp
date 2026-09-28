// What is armed into the radio and when: a burst goes into the dwell already on air, a guard
// phase gets nothing, and a plan refused or held is counted as what it was.
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

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
// Queued, a plan like it would also have taken the place of the next dwell, which then never ran.
TEST_CASE("rf: a plan that could only run inside the flying dwell is refused, not queued") {
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

    ports::RfPlan next = flying;
    next.freq_hz = timing::kMband1Hz;
    next.start_us = 800000;
    next.end_us = 1200000;
    REQUIRE(rf.arm(next) == Status::Ok);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan stranded = flying;
    stranded.freq_hz = timing::kMband1Hz;
    stranded.start_us = 455000;
    stranded.tx = frame;
    stranded.tx_len = sizeof(frame);
    stranded.tx_at_us = 600000;
    CHECK(rf.arm(stranded) == Status::WouldBlock);

    ports::RfPlan listening = flying;
    listening.freq_hz = timing::kMband1Hz;
    listening.start_us = 455000;
    listening.end_us = 600000;
    CHECK(rf.arm(listening) == Status::WouldBlock);

    for (uint32_t t = 460; t <= 790; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    // The dwell in flight kept the channel it was armed for, and the burst never went on air.
    CHECK(tuned_khz(chip) == timing::kMband0Hz / 1000);
    CHECK_FALSE(chip.tx_pending);

    for (uint32_t t = 800; t <= 850; t += 10) {
        clock.set_millis(t);
        rf.service(t);
    }
    CHECK(tuned_khz(chip) == timing::kMband1Hz / 1000);
    int missed = 0;
    events::RfEvent e{};
    while (events.pop(e))
        if (e.type == events::RfEventType::Missed) missed++;
    CHECK(missed == 0);
}

// The next dwell is retuned into as soon as the one before it ends, so the guard
// between them is spent switching and the dwell is listening at its start.
TEST_CASE("rf: a queued dwell is switched into when the one before it ends, ahead of its start") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan uplink{};
    uplink.mode = ports::RfMode::RxOband;
    uplink.freq_hz = timing::kObandHz;
    uplink.start_us = 205000;
    uplink.end_us = 395000;
    ports::RfPlan slot0 = uplink;
    slot0.mode = ports::RfMode::RxMband;
    slot0.freq_hz = timing::kMband0Hz;
    slot0.start_us = 400000;
    slot0.end_us = 799000;
    clock.set_millis(200);
    REQUIRE(rf.arm(uplink) == Status::Ok);
    REQUIRE(rf.arm(slot0) == Status::Ok);
    rf.service(200);
    REQUIRE(tuned_khz(chip) == timing::kObandHz / 1000);

    clock.set_millis(395);
    rf.service(395);
    CHECK(tuned_khz(chip) == timing::kMband0Hz / 1000);
    CHECK(chip.receiving);
    CHECK(rf.switching().late == 0);
}

// A dwell armed ahead that the executor only reached after its start is the fault this
// counts, read out on the bench rather than inferred from a burst that went out late.
TEST_CASE("rf: a dwell armed ahead and reached after its start is counted late") {
    models::Sx1262 chip;
    parts::Sx1262 radio(chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin);
    platform::host::Clock clock;
    bus::Queue<events::RfEvent, 8> events;
    platform::host::Rf rf(radio, clock, events);
    REQUIRE(rf.begin() == Status::Ok);

    ports::RfPlan slot1{};
    slot1.mode = ports::RfMode::RxMband;
    slot1.freq_hz = timing::kMband1Hz;
    slot1.start_us = 800000;
    slot1.end_us = 1200000;
    clock.set_millis(500);
    REQUIRE(rf.arm(slot1) == Status::Ok);
    clock.set_millis(810);
    rf.service(810);

    CHECK(tuned_khz(chip) == timing::kMband1Hz / 1000);
    CHECK(rf.switching().late == 1);
}

namespace {

// Silicon's clock moves while a pass runs, and the host's stands still unless told to creep.
struct CreepingClock : platform::host::Clock {
    uint64_t micros() const override {
        const uint64_t now_us = Clock::micros() + crept_us_;
        crept_us_ += creep_us;
        return now_us;
    }
    void set_micros(uint64_t us) {
        Clock::set_micros(us);
        crept_us_ = 0;
    }
    uint64_t creep_us{0};

   private:
    mutable uint64_t crept_us_{0};
};

// The policy alone, with every plan it hands the executor kept for reading.
struct Armings {
    CreepingClock clock{};
    struct Recorder : ports::Rf {
        Status begin() override { return Status::Ok; }
        Status arm(const ports::RfPlan& plan) override {
            last = plan;
            arms++;
            if (plan.tx != nullptr) with_tx++;
            if (!refuse && handed < kKept) {
                plans[handed] = plan;
                armed_at_us[handed] = clock->micros();
                handed++;
            }
            return refuse ? Status::OutOfRange : Status::Ok;
        }
        void abort() override { aborts++; }
        static constexpr uint32_t kKept = 32;
        platform::host::Clock* clock{nullptr};
        ports::RfPlan last{};
        ports::RfPlan plans[kKept]{};
        uint64_t armed_at_us[kKept]{};
        uint32_t handed{0};
        uint32_t arms{0};
        uint32_t with_tx{0};
        uint32_t aborts{0};
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

    Armings() { rf.clock = &clock; }

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
TEST_CASE("rf: every dwell is handed over whole, never as a stub inside a guard") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 2000; phase++) a.tick_in(phase / 1000, phase % 1000);

    REQUIRE(a.rf.handed >= 6);
    for (uint32_t i = 0; i < a.rf.handed; i++) {
        const ports::RfPlan& plan = a.rf.plans[i];
        CHECK(plan.end_us - plan.start_us >= 190000);
    }
}

// Armed at the edge by whichever pass noticed it, a dwell opened up to a pass
// late and a burst drawn for its first milliseconds was gone before it was armed.
TEST_CASE("rf: the next dwell is armed ahead of its edge, at the edge the latched PPS names") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 2000; phase += 7) a.tick_in(phase / 1000, phase % 1000);

    int slot1_dwells = 0;
    for (uint32_t i = 1; i < a.rf.handed; i++) {
        const ports::RfPlan& plan = a.rf.plans[i];
        CHECK(a.rf.armed_at_us[i] < plan.start_us);
        CHECK((plan.start_us - Armings::kEdgeUs) % 1000 == 0);
        if (plan.freq_hz != timing::kMband1Hz) continue;
        slot1_dwells++;
        CHECK((plan.start_us - Armings::kEdgeUs) % 1000000 == 800000);
        CHECK(plan.end_us - plan.start_us == 400000);
    }
    CHECK(slot1_dwells >= 2);
}

// A slot-1 burst drawn for 800 ms used to be dropped: its dwell was only armed by
// the first pass after 800, and a burst whose instant had gone by was not armed.
TEST_CASE("rf: a slot 1 burst decided while slot 0 flies is queued with its dwell") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    a.ready_to_transmit(1001);
    a.state.clock.utc_s = 1001;
    a.state.clock.utc_edge_us = Armings::kEdgeUs;

    for (int phase = 0; phase < 800; phase += 10) a.tick_at(phase);

    const ports::RfPlan* queued = nullptr;
    for (uint32_t i = 0; i < a.rf.handed; i++)
        if (a.rf.plans[i].freq_hz == timing::kMband1Hz && a.rf.plans[i].tx != nullptr)
            queued = &a.rf.plans[i];
    REQUIRE(queued != nullptr);
    CHECK(queued->start_us == Armings::kEdgeUs + 800000);
    CHECK(queued->tx_at_us >= queued->start_us);
    CHECK(queued->tx_at_us < Armings::kEdgeUs + 1000000);
}

// A pass read the hop plan at 799.99 ms, promoted slot 1 after 800, and aborted it in flight.
TEST_CASE("rf: a pass that straddles a dwell edge keeps the dwell already queued for it") {
    Armings a;
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 800; phase += 10) a.tick_at(phase);
    const uint32_t handed = a.rf.handed;

    a.clock.set_micros(Armings::kEdgeUs + 799990);
    a.clock.creep_us = 5;
    a.radio.tick(static_cast<uint32_t>((Armings::kEdgeUs + 799990) / 1000));
    a.clock.creep_us = 0;
    a.tick_at(810);

    CHECK(a.rf.aborts == 0);
    REQUIRE(a.rf.handed > handed);
    for (uint32_t i = handed; i < a.rf.handed; i++)
        CHECK(a.rf.plans[i].freq_hz != timing::kMband1Hz);
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

    REQUIRE(a.rf.with_tx >= 1);
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

namespace {

constexpr uint64_t kSecondOneUs = Armings::kEdgeUs + 1000000;
constexpr uint64_t kBurstUs = 6000;

struct Outcomes {
    Armings& a;
    uint64_t held_at_us{0};
    std::vector<uint64_t> reported{};

    void report_flown() {
        const uint64_t now_us = a.clock.micros();
        for (uint32_t i = 0; i < a.rf.handed; i++) {
            const ports::RfPlan& plan = a.rf.plans[i];
            if (plan.tx == nullptr || plan.tx_at_us + kBurstUs > now_us) continue;
            if (plan.tx_at_us != held_at_us && !was_reported(plan.tx_at_us)) report(plan.tx_at_us);
        }
    }

    void report(uint64_t tx_at_us) {
        a.state.rf.note_tx_done(a.state.air.tx_ok, tx_at_us);
        a.state.air.tx_ok++;
        reported.push_back(tx_at_us);
    }

    bool was_reported(uint64_t tx_at_us) const {
        return std::find(reported.begin(), reported.end(), tx_at_us) != reported.end();
    }

    uint64_t name_at_us() const {
        for (uint32_t i = 0; i < a.rf.handed; i++) {
            const ports::RfPlan& plan = a.rf.plans[i];
            if (plan.tx != nullptr && plan.tx_at_us >= kSecondOneUs &&
                plan.tx_at_us < kSecondOneUs + timing::kSlot1Wrap * 1000)
                return plan.tx_at_us;
        }
        return 0;
    }
};

void fly_ground_second(Armings& a, Outcomes& outcomes) {
    std::strncpy(a.settings.callsign, "D-KXYZ", go::kCallsignCap - 1);
    a.clock.set_micros(Armings::kEdgeUs);
    REQUIRE(a.radio.setup() == Status::Ok);
    const uint32_t utc = 1000 + a.radio.transmitter().ground_second();
    a.ready_to_transmit(utc);
    a.state.clock.utc_s = utc;
    for (int phase = 0; phase < 1000; phase += 10) {
        a.tick_at(phase);
        outcomes.report_flown();
    }
    outcomes.held_at_us = outcomes.name_at_us();
    REQUIRE(outcomes.held_at_us != 0);
    a.state.own.utc = utc + 1;
    a.state.clock.utc_s = utc + 1;
}

}  // namespace

// The name's dwell closes 5 ms after its latest instant, and its TxDone reaches the policy a pass
// later: counted missed at the close, it was then credited to the next dwell as a position.
TEST_CASE("rf: a TxDone that lands after its dwell closed is credited to the burst it names") {
    Armings a;
    Outcomes outcomes{a};
    fly_ground_second(a, outcomes);

    for (int phase = 0; phase <= 200; phase += 10) {
        a.tick_in(1, phase);
        outcomes.report_flown();
    }
    outcomes.report(outcomes.held_at_us);
    a.tick_in(1, 210);

    CHECK(a.state.rf.timing_stats.missed() == 0);
    CHECK(a.state.air.tx_named == 1);
    CHECK(a.radio.transmitter().sent_count() == outcomes.reported.size());
}

TEST_CASE("rf: a burst that never reports is counted missed once its report is overdue") {
    Armings a;
    Outcomes outcomes{a};
    fly_ground_second(a, outcomes);

    const int overdue_ms = timing::kSlot1Wrap + static_cast<int>(go::RadioService::kTxOutcomeMaxAgeMs);
    for (int phase = 0; phase < overdue_ms; phase += 10) {
        a.tick_in(1, phase);
        outcomes.report_flown();
    }
    CHECK(a.state.rf.timing_stats.missed() == 0);

    a.tick_in(1, overdue_ms);
    CHECK(a.state.rf.timing_stats.missed() == 1);
    CHECK(a.state.air.tx_named == 0);
}
