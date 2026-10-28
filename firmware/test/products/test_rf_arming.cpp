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
#include "hardware/platform/host/identity.h"
#include "products/skyblip_go/services/traffic.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

// The PLL word the driver writes back-converts a hertz or two short of the channel it asked for.
uint32_t tuned_khz(const models::Sx1262& chip) { return (chip.frequency_hz + 500) / 1000; }

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
    flying.mode = ports::RfMode::RxMBand;
    flying.frequency_hz = timing::kMBand0Hz;
    flying.start_us = 400000;
    flying.end_us = 799000;
    REQUIRE(rf.arm(flying) == Status::Ok);
    for (uint32_t now_ms = 400; now_ms <= 450; now_ms += 10) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
    }
    REQUIRE(tuned_khz(chip) == timing::kMBand0Hz / 1000);

    ports::RfPlan next = flying;
    next.frequency_hz = timing::kMBand1Hz;
    next.start_us = 800000;
    next.end_us = 1200000;
    REQUIRE(rf.arm(next) == Status::Ok);

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan stranded = flying;
    stranded.frequency_hz = timing::kMBand1Hz;
    stranded.start_us = 455000;
    stranded.tx = frame;
    stranded.tx_bytes = sizeof(frame);
    stranded.tx_at_us = 600000;
    CHECK(rf.arm(stranded) == Status::WouldBlock);

    ports::RfPlan listening = flying;
    listening.frequency_hz = timing::kMBand1Hz;
    listening.start_us = 455000;
    listening.end_us = 600000;
    CHECK(rf.arm(listening) == Status::WouldBlock);

    for (uint32_t now_ms = 460; now_ms <= 790; now_ms += 10) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
    }
    // The dwell in flight kept the channel it was armed for, and the burst never went on air.
    CHECK(tuned_khz(chip) == timing::kMBand0Hz / 1000);
    CHECK_FALSE(chip.tx_pending);

    for (uint32_t now_ms = 800; now_ms <= 850; now_ms += 10) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
    }
    CHECK(tuned_khz(chip) == timing::kMBand1Hz / 1000);
    int missed = 0;
    events::RfEvent event{};
    while (events.pop(event))
        if (event.type == events::RfEventType::Missed) missed++;
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
    uplink.mode = ports::RfMode::RxOBand;
    uplink.frequency_hz = timing::kOBandHz;
    uplink.start_us = 205000;
    uplink.end_us = 395000;
    ports::RfPlan slot0 = uplink;
    slot0.mode = ports::RfMode::RxMBand;
    slot0.frequency_hz = timing::kMBand0Hz;
    slot0.start_us = 400000;
    slot0.end_us = 799000;
    clock.set_milliseconds(200);
    REQUIRE(rf.arm(uplink) == Status::Ok);
    REQUIRE(rf.arm(slot0) == Status::Ok);
    rf.service(200);
    REQUIRE(tuned_khz(chip) == timing::kOBandHz / 1000);

    clock.set_milliseconds(395);
    rf.service(395);
    CHECK(tuned_khz(chip) == timing::kMBand0Hz / 1000);
    CHECK(chip.receiving);
    CHECK(rf.switching().late_starts == 0);
    const ports::RfSwitch& change = rf.switching().last;
    CHECK(change.from == ports::RfMode::RxOBand);
    CHECK(change.to == ports::RfMode::RxMBand);
    CHECK(change.margin_us() == 5000);
    CHECK(change.gap_us() == 0);
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
    slot1.mode = ports::RfMode::RxMBand;
    slot1.frequency_hz = timing::kMBand1Hz;
    slot1.start_us = 800000;
    slot1.end_us = 1200000;
    clock.set_milliseconds(500);
    REQUIRE(rf.arm(slot1) == Status::Ok);
    clock.set_milliseconds(810);
    rf.service(810);

    CHECK(tuned_khz(chip) == timing::kMBand1Hz / 1000);
    CHECK(rf.switching().late_starts == 1);
    CHECK(rf.switching().last.late_start());
    CHECK(rf.switching().last.margin_us() == -10000);
}

namespace {

// Silicon's clock moves while a pass runs, and the host's stands still unless told to creep.
struct CreepingClock : platform::host::Clock {
    uint64_t microseconds() const override {
        const uint64_t now_us = Clock::microseconds() + crept_us_;
        crept_us_ += creep_us;
        return now_us;
    }
    void set_microseconds(uint64_t now_us) {
        Clock::set_microseconds(now_us);
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
                armed_at_us[handed] = clock->microseconds();
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
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null{};
    ports::Roles roles{clock,
                       rf,
                       null.link,
                       null.display,
                       null.key_value,
                       null.log_flash,
                       null.annunciator,
                       null.dfu,
                       null.die_temperature,
                       null.indicator,
                       null.gnss,
                       identity,
                       ports::Capability::Rf};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::RadioService radio{context, settings};

    Armings() {
        rf.clock = &clock;
        state.activation.serial = 0x7E57;
    }

    static constexpr uint64_t kEdgeUs = 30000000;

    void tick_at(int phase_ms) { tick_in(0, phase_ms); }

    void tick_in(uint32_t second, int phase_ms) {
        const uint64_t edge_us = kEdgeUs + static_cast<uint64_t>(second) * 1000000;
        const uint64_t now_us = edge_us + static_cast<uint64_t>(phase_ms) * 1000;
        clock.set_microseconds(now_us);
        state.clock.pps_locked = true;
        state.clock.utc_valid = true;
        state.clock.pps_edge_us = edge_us;
        radio.tick(static_cast<uint32_t>(now_us / 1000));
    }

    void ready_to_transmit(uint32_t utc_s) {
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.tx_settled = true;
        state.own.flight_state = 2;
        state.own.utc_s = utc_s;
        state.own.fix_at_ms = static_cast<uint32_t>(kEdgeUs / 1000);
    }
};

}  // namespace

// The guard phases between dwells report the dwell that has just closed, and a
// window already behind the phase used to be armed as a 1 ms stub the executor
// then dropped: one fabricated LOST per pass that landed in a guard.
TEST_CASE("rf: every dwell is handed over whole, never as a stub inside a guard") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 2000; phase++) armings.tick_in(phase / 1000, phase % 1000);

    REQUIRE(armings.rf.handed >= 6);
    for (uint32_t i = 0; i < armings.rf.handed; i++) {
        const ports::RfPlan& plan = armings.rf.plans[i];
        CHECK(plan.end_us - plan.start_us >= 190000);
    }
}

// Armed at the edge by whichever pass noticed it, a dwell opened up to a pass
// late and a burst drawn for its first milliseconds was gone before it was armed.
TEST_CASE("rf: the next dwell is armed ahead of its edge, at the edge the latched PPS names") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 2000; phase += 7) armings.tick_in(phase / 1000, phase % 1000);

    int slot1_dwells = 0;
    for (uint32_t i = 1; i < armings.rf.handed; i++) {
        const ports::RfPlan& plan = armings.rf.plans[i];
        CHECK(armings.rf.armed_at_us[i] < plan.start_us);
        CHECK((plan.start_us - Armings::kEdgeUs) % 1000 == 0);
        if (plan.frequency_hz != timing::kMBand1Hz) continue;
        slot1_dwells++;
        CHECK((plan.start_us - Armings::kEdgeUs) % 1000000 == 800000);
        CHECK(plan.end_us - plan.start_us == 400000);
    }
    CHECK(slot1_dwells >= 2);
}

// A slot-1 burst drawn for 800 ms used to be dropped: its dwell was only armed by
// the first pass after 800, and a burst whose instant had gone by was not armed.
TEST_CASE("rf: a slot 1 burst decided while slot 0 flies is queued with its dwell") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.ready_to_transmit(1001);
    armings.state.clock.utc_s = 1001;
    armings.state.clock.utc_edge_us = Armings::kEdgeUs;

    for (int phase = 0; phase < 800; phase += 10) armings.tick_at(phase);

    const ports::RfPlan* queued = nullptr;
    for (uint32_t i = 0; i < armings.rf.handed; i++)
        if (armings.rf.plans[i].frequency_hz == timing::kMBand1Hz &&
            armings.rf.plans[i].tx != nullptr)
            queued = &armings.rf.plans[i];
    REQUIRE(queued != nullptr);
    CHECK(queued->start_us == Armings::kEdgeUs + 800000);
    CHECK(queued->tx_at_us >= queued->start_us);
    CHECK(queued->tx_at_us < Armings::kEdgeUs + 1000000);
}

// A pass read the hop plan at 799.99 ms, promoted slot 1 after 800, and aborted it in flight.
TEST_CASE("rf: a pass that straddles a dwell edge keeps the dwell already queued for it") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    for (int phase = 0; phase < 800; phase += 10) armings.tick_at(phase);
    const uint32_t handed = armings.rf.handed;

    armings.clock.set_microseconds(Armings::kEdgeUs + 799990);
    armings.clock.creep_us = 5;
    armings.radio.tick(static_cast<uint32_t>((Armings::kEdgeUs + 799990) / 1000));
    armings.clock.creep_us = 0;
    armings.tick_at(810);

    CHECK(armings.rf.aborts == 0);
    REQUIRE(armings.rf.handed > handed);
    for (uint32_t i = handed; i < armings.rf.handed; i++)
        CHECK(armings.rf.plans[i].frequency_hz != timing::kMBand1Hz);
}

// Whether a burst may go out is decided on the fix, the rate and the slot, and
// those clear when they clear: a solution that lands after the dwell opened used
// to cost the whole second, because the dwell was armed at its edge and never
// looked at again.
TEST_CASE("rf: a burst whose gates clear inside the dwell is armed into that dwell") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.state.own.utc_valid = true;
    armings.state.own.tx_settled = true;
    armings.state.own.flight_state = 2;
    armings.state.own.fix_at_ms = static_cast<uint32_t>(Armings::kEdgeUs / 1000);

    armings.tick_at(400);
    const uint32_t armed_at_the_edge = armings.rf.arms;
    CHECK(armings.rf.last.tx == nullptr);

    armings.state.own.fix_valid = true;
    armings.tick_at(410);
    CHECK(armings.rf.arms == armed_at_the_edge + 1);
    REQUIRE(armings.rf.last.tx != nullptr);
    CHECK(armings.rf.last.tx_at_us >= armings.rf.last.start_us);
    CHECK(armings.rf.last.tx_at_us < armings.rf.last.end_us);

    for (int phase = 420; phase < 790; phase += 10) armings.tick_at(phase);
    CHECK(armings.rf.arms == armed_at_the_edge + 1);
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
    dwell.mode = ports::RfMode::RxMBand;
    dwell.frequency_hz = timing::kMBand0Hz;
    dwell.start_us = 400000;
    dwell.end_us = 799000;
    REQUIRE(rf.arm(dwell) == Status::Ok);
    for (uint32_t now_ms = 400; now_ms <= 450; now_ms += 10) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
    }

    const uint8_t frame[protocol::AdslPacket::kTxBytes] = {0x72, 0x4B};
    ports::RfPlan burst = dwell;
    burst.start_us = 455000;
    burst.tx = frame;
    burst.tx_bytes = sizeof(frame);
    burst.tx_at_us = 600000;
    REQUIRE(rf.arm(burst) == Status::Ok);

    uint8_t on_air[events::kRfEventBytes];
    uint8_t on_air_bytes = 0;
    for (uint32_t now_ms = 460; now_ms <= 800; now_ms += 10) {
        clock.set_milliseconds(now_ms);
        rf.service(now_ms);
        if (chip.take_tx(on_air, on_air_bytes)) chip.signal_tx_done();
    }

    CHECK(on_air_bytes > 0);
    CHECK(tuned_khz(chip) == timing::kMBand0Hz / 1000);
    int sent = 0, missed = 0;
    events::RfEvent event{};
    while (events.pop(event)) {
        if (event.type == events::RfEventType::TxDone) sent++;
        if (event.type == events::RfEventType::Missed) missed++;
    }
    CHECK(sent == 1);
    CHECK(missed == 0);
}

// A burst refused before it was ever armed left the counters moving and the page
// silent, which reads exactly like a dead transmitter to the one person looking.
TEST_CASE("rf: a plan the radio refuses is a burst that never armed, not one that went missing") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.ready_to_transmit(1000);
    armings.rf.refuse = true;

    armings.tick_at(400);

    REQUIRE(armings.rf.with_tx >= 1);
    REQUIRE(armings.state.radio_log.count() == 1);
    CHECK(armings.state.radio_log.newest(0).verdict == radio::Verdict::Unarmed);
    CHECK(armings.state.rf.timing_statistics.missed() == 1);
}

// The hour's allowance holds every burst until it frees up again, so one row says
// when that began: a spell of them would push the sky itself off a 16-row tape.
TEST_CASE("rf: the hour's air-time budget holding a burst is said once, and counted every time") {
    Armings armings;
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    armings.ready_to_transmit(1000);

    // EN 300 220-2 band M is 10 permille of the hour: 7200 bursts of 5 ms fill it.
    for (int i = 0; i < 7202; i++) {
        armings.state.air.tx_ok++;
        armings.tick_at(100);
    }
    REQUIRE(armings.state.radio_log.count() == 0);

    armings.state.own.utc_s += 2;  // the lower channel's second again
    armings.tick_at(400);

    REQUIRE(armings.state.radio_log.count() == 1);
    CHECK(armings.state.radio_log.newest(0).verdict == radio::Verdict::Held);
    CHECK(armings.state.radio_log.newest(0).band == model::Band::M);
    CHECK(armings.state.rf.timing_statistics.refused() == 1);

    armings.state.own.utc_s += 2;
    armings.state.own.fix_at_ms = static_cast<uint32_t>((Armings::kEdgeUs + 1000000) / 1000);
    armings.tick_in(1, 205);
    armings.tick_in(1, 400);

    CHECK(armings.state.radio_log.count() == 1);
    CHECK(armings.state.rf.timing_statistics.refused() == 2);
}

namespace {

constexpr uint64_t kSecondOneUs = Armings::kEdgeUs + 1000000;
constexpr uint64_t kBurstUs = 6000;

struct Outcomes {
    Armings& armings;
    uint64_t held_at_us{0};
    std::vector<uint64_t> reported{};

    void report_flown() {
        const uint64_t now_us = armings.clock.microseconds();
        for (uint32_t i = 0; i < armings.rf.handed; i++) {
            const ports::RfPlan& plan = armings.rf.plans[i];
            if (plan.tx == nullptr || plan.tx_at_us + kBurstUs > now_us) continue;
            if (plan.tx_at_us != held_at_us && !was_reported(plan.tx_at_us)) report(plan.tx_at_us);
        }
    }

    void report(uint64_t tx_at_us) {
        armings.state.rf.note_tx_done(armings.state.air.tx_ok, tx_at_us);
        armings.state.air.tx_ok++;
        reported.push_back(tx_at_us);
    }

    bool was_reported(uint64_t tx_at_us) const {
        return std::find(reported.begin(), reported.end(), tx_at_us) != reported.end();
    }

    uint64_t name_at_us() const {
        for (uint32_t i = 0; i < armings.rf.handed; i++) {
            const ports::RfPlan& plan = armings.rf.plans[i];
            if (plan.tx != nullptr && plan.tx_at_us >= kSecondOneUs &&
                plan.tx_at_us < kSecondOneUs + timing::kSlot1WrapMs * 1000)
                return plan.tx_at_us;
        }
        return 0;
    }
};

void fly_ground_second(Armings& armings, Outcomes& outcomes) {
    std::strncpy(armings.settings.callsign, "D-KXYZ", go::kCallsignBytes - 1);
    armings.clock.set_microseconds(Armings::kEdgeUs);
    REQUIRE(armings.radio.setup() == Status::Ok);
    const uint32_t utc_s = 1000 + armings.radio.transmitter().ground_second();
    armings.ready_to_transmit(utc_s);
    armings.state.clock.utc_s = utc_s;
    for (int phase = 0; phase < 1000; phase += 10) {
        armings.tick_at(phase);
        outcomes.report_flown();
    }
    outcomes.held_at_us = outcomes.name_at_us();
    REQUIRE(outcomes.held_at_us != 0);
    armings.state.own.utc_s = utc_s + 1;
    armings.state.clock.utc_s = utc_s + 1;
}

}  // namespace

// The name's dwell closes 5 ms after its latest instant, and its TxDone reaches the policy a pass
// later: counted missed at the close, it was then credited to the next dwell as a position.
TEST_CASE("rf: a TxDone that lands after its dwell closed is credited to the burst it names") {
    Armings armings;
    Outcomes outcomes{armings};
    fly_ground_second(armings, outcomes);

    for (int phase = 0; phase <= 200; phase += 10) {
        armings.tick_in(1, phase);
        outcomes.report_flown();
    }
    outcomes.report(outcomes.held_at_us);
    armings.tick_in(1, 210);

    CHECK(armings.state.rf.timing_statistics.missed() == 0);
    CHECK(armings.state.air.tx_named == 1);
    CHECK(armings.radio.transmitter().sent_count() == outcomes.reported.size());
}

TEST_CASE("rf: a burst that never reports is counted missed once its report is overdue") {
    Armings armings;
    Outcomes outcomes{armings};
    fly_ground_second(armings, outcomes);

    const int overdue_ms =
        timing::kSlot1WrapMs + static_cast<int>(go::RadioService::kTxOutcomeMaxAgeMs);
    for (int phase = 0; phase < overdue_ms; phase += 10) {
        armings.tick_in(1, phase);
        outcomes.report_flown();
    }
    CHECK(armings.state.rf.timing_statistics.missed() == 0);

    armings.tick_in(1, overdue_ms);
    CHECK(armings.state.rf.timing_statistics.missed() == 1);
    CHECK(armings.state.air.tx_named == 0);
}
