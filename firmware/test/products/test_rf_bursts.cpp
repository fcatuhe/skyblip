// Two bursts in one dwell: the ground second's position and the name in slot 1's tail.
#include <cstring>
#include <vector>

#include "core/events/rf.h"
#include "core/radio/log.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint8_t kPosition = 0x50;
constexpr uint8_t kName = 0x4E;

struct Keyed {
    uint8_t frame;
    uint32_t at_ms;
};

// The executor alone, with the chip finishing each burst the millisecond after it keys.
struct Executor {
    models::Sx1262 chip{};
    parts::Sx1262 radio{chip, chip, chip, chip.busy_pin, chip.reset_pin, chip.dio1_pin};
    platform::host::Clock clock{};
    bus::Queue<events::RfEvent, 8> events{};
    platform::host::Rf rf{radio, clock, events};
    uint8_t position[protocol::AdslPacket::kTxBytes] = {kPosition};
    uint8_t name[protocol::AdslPacket::kTxBytes] = {kName};
    bool completes{true};

    static ports::RfPlan slot1() {
        ports::RfPlan plan{};
        plan.mode = ports::RfMode::RxMband;
        plan.freq_hz = timing::kMband1Hz;
        plan.start_us = 800000;
        plan.end_us = 1200000;
        return plan;
    }

    ports::RfPlan carrying(const uint8_t* frame, uint64_t at_us) const {
        ports::RfPlan plan = slot1();
        plan.tx = frame;
        plan.tx_len = protocol::AdslPacket::kTxBytes;
        plan.tx_at_us = at_us;
        return plan;
    }

    std::vector<Keyed> fly(uint32_t from_ms, uint32_t to_ms) {
        std::vector<Keyed> keyed;
        for (uint32_t t = from_ms; t <= to_ms; t++) {
            clock.set_millis(t);
            rf.service(t);
            uint8_t sent[protocol::kTxPayloadChipBytes];
            uint8_t len = 0;
            if (!completes || !chip.take_tx(sent, len)) continue;
            keyed.push_back({sent[len - protocol::AdslPacket::kTxBytes], t});
            chip.signal_tx_done();
        }
        return keyed;
    }

    std::vector<events::RfEvent> drained(events::RfEventType type) {
        std::vector<events::RfEvent> out;
        events::RfEvent e{};
        while (events.pop(e))
            if (e.type == type) out.push_back(e);
        return out;
    }
};

}  // namespace

// The position's gate clears on the fix, so it is armed after the name that was decided at 400.
TEST_CASE("rf: a dwell keys both its bursts in time order, whichever was armed first") {
    Executor x;
    REQUIRE(x.rf.begin() == Status::Ok);
    x.clock.set_millis(790);
    REQUIRE(x.rf.arm(x.carrying(x.name, 1050000)) == Status::Ok);
    REQUIRE(x.rf.arm(x.carrying(x.position, 900000)) == Status::Ok);

    const std::vector<Keyed> keyed = x.fly(790, 1199);

    REQUIRE(keyed.size() == 2);
    CHECK(keyed[0].frame == kPosition);
    CHECK(keyed[0].at_ms == 900);
    CHECK(keyed[1].frame == kName);
    CHECK(keyed[1].at_ms == 1050);
    const std::vector<events::RfEvent> done = x.drained(events::RfEventType::TxDone);
    REQUIRE(done.size() == 2);
    CHECK(done[0].tx_at_us == 900000);
    CHECK(done[1].tx_at_us == 1050000);
}

// Replacing the queued plan was how the name was lost when the position was queued after it.
TEST_CASE("rf: a burst armed for the queued dwell joins it instead of replacing it") {
    Executor x;
    REQUIRE(x.rf.begin() == Status::Ok);
    ports::RfPlan slot0 = Executor::slot1();
    slot0.freq_hz = timing::kMband0Hz;
    slot0.start_us = 400000;
    slot0.end_us = 797000;
    x.clock.set_millis(400);
    REQUIRE(x.rf.arm(slot0) == Status::Ok);
    x.rf.service(400);

    REQUIRE(x.rf.arm(x.carrying(x.name, 1100000)) == Status::Ok);
    REQUIRE(x.rf.arm(x.carrying(x.position, 850000)) == Status::Ok);
    const std::vector<Keyed> keyed = x.fly(401, 1199);

    REQUIRE(keyed.size() == 2);
    CHECK(keyed[0].frame == kPosition);
    CHECK(keyed[1].frame == kName);
}

TEST_CASE("rf: a burst armed twice for its dwell goes on air once") {
    Executor x;
    REQUIRE(x.rf.begin() == Status::Ok);
    x.clock.set_millis(790);
    REQUIRE(x.rf.arm(x.carrying(x.position, 900000)) == Status::Ok);
    REQUIRE(x.rf.arm(x.carrying(x.position, 900000)) == Status::Ok);

    CHECK(x.fly(790, 1199).size() == 1);
    CHECK(x.drained(events::RfEventType::Missed).empty());
}

// A lost burst is reported by the instant it was armed for, so the tape can say which one it was.
TEST_CASE("rf: a dwell that closes on two unfinished bursts reports each lost, by its instant") {
    Executor x;
    x.completes = false;
    REQUIRE(x.rf.begin() == Status::Ok);
    x.clock.set_millis(790);
    REQUIRE(x.rf.arm(x.carrying(x.position, 900000)) == Status::Ok);
    REQUIRE(x.rf.arm(x.carrying(x.name, 1150000)) == Status::Ok);

    x.fly(790, 1200);

    const std::vector<events::RfEvent> lost = x.drained(events::RfEventType::Missed);
    REQUIRE(lost.size() == 2);
    CHECK(lost[0].tx_at_us == 900000);
    CHECK(lost[0].keyed_at_us != 0);
    // The position never finished, so the name behind it never keyed.
    CHECK(lost[1].tx_at_us == 1150000);
    CHECK(lost[1].keyed_at_us == 0);
}

TEST_CASE("rf: a dwell carries two bursts and refuses a third") {
    Executor x;
    REQUIRE(x.rf.begin() == Status::Ok);
    x.clock.set_millis(790);
    REQUIRE(x.rf.arm(x.carrying(x.position, 900000)) == Status::Ok);
    REQUIRE(x.rf.arm(x.carrying(x.name, 1050000)) == Status::Ok);
    CHECK(x.rf.arm(x.carrying(x.name, 1150000)) == Status::WouldBlock);
}

// Every other ground second puts the position in slot 1, the dwell the name's tail is part of.
TEST_CASE("rf: on the ground the name follows its own position, sharing slot 1 when they must") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    std::strncpy(h.product().settings().callsign, "D-KXYZ", go::kCallsignCap - 1);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    run_on(h, past_settling(h), 40000);

    const simulator::Air& air = h.world().air();
    int positions = 0, names = 0, shared_dwell = 0;
    uint64_t position_at_us = 0;
    bool position_on_channel1 = false;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        if (timing::Scheduler::in_direct_slot(r.phase_ms)) {
            positions++;
            position_at_us = r.at_us;
            position_on_channel1 = simulator::Air::tuned_to(r.freq_hz, timing::kMband1Hz);
            continue;
        }
        names++;
        CAPTURE(r.at_us);
        // Its own ground second's position, 450..990 ms into the second the name's tail closes.
        CHECK(position_at_us != 0);
        CHECK(r.at_us - position_at_us < 1000000 - timing::kDirectStart * 1000);
        if (position_on_channel1) shared_dwell++;
    }
    CHECK(names >= 3);
    CHECK(positions == names);
    CHECK(shared_dwell >= 1);

    // Two bursts closing in one dwell go on the tape as what they were, each at its own instant.
    const radio::Log& log = h.product().state().radio_log;
    int rows = 0;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& e = log.newest(i);
        if (e.event != radio::Event::Transmitted) continue;
        rows++;
        CAPTURE(e.at_s);
        CHECK(e.callsign == (e.into_ms < timing::kSlot1Wrap));
        // one 5 ms host step late at most, a neighbour's instant would be 10 ms or more off
        CHECK(e.tx_keyed_us < simulator::Simulator::kStepMs * 1000);
    }
    CHECK(rows >= 4);
}
