// Two bursts in one dwell, as the executor flies them, and the name in slot 1's tail.
#include <cstring>
#include <vector>

#include "core/events/rf.h"
#include "core/radio/log.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "ports/rf.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint8_t kPositionMarker = 0x50;
constexpr uint8_t kNameMarker = 0x4E;

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
    uint8_t position[adsl::Packet::kTxBytes] = {kPositionMarker};
    uint8_t name[adsl::Packet::kTxBytes] = {kNameMarker};
    bool completes{true};

    static ports::RfPlan slot1() {
        ports::RfPlan plan{};
        plan.mode = ports::RfMode::RxMBand;
        plan.frequency_hz = adsl::kMBand1Hz;
        plan.start_us = 800000;
        plan.end_us = 1200000;
        return plan;
    }

    ports::RfPlan carrying(const uint8_t* frame, uint64_t at_us) const {
        ports::RfPlan plan = slot1();
        plan.tx = frame;
        plan.tx_bytes = adsl::Packet::kTxBytes;
        plan.tx_at_us = at_us;
        return plan;
    }

    std::vector<Keyed> fly(uint32_t from_ms, uint32_t to_ms) {
        std::vector<Keyed> keyed;
        for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms++) {
            clock.set_milliseconds(now_ms);
            rf.service(now_ms);
            uint8_t sent[protocol::kTxPayloadChipBytes];
            uint8_t bytes = 0;
            if (!completes || !chip.take_tx(sent, bytes)) continue;
            keyed.push_back({sent[bytes - adsl::Packet::kTxBytes], now_ms});
            chip.signal_tx_done();
        }
        return keyed;
    }

    std::vector<events::RfEvent> drained(events::RfEventType type) {
        std::vector<events::RfEvent> out;
        events::RfEvent event{};
        while (events.pop(event))
            if (event.type == type) out.push_back(event);
        return out;
    }
};

}  // namespace

// The position's gate clears on the fix, so it is armed after the name that was decided at 400.
TEST_CASE("rf: a dwell keys both its bursts in time order, whichever was armed first") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.clock.set_milliseconds(790);
    REQUIRE(executor.rf.arm(executor.carrying(executor.name, 1050000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 900000)) == Status::Ok);

    const std::vector<Keyed> keyed = executor.fly(790, 1199);

    REQUIRE(keyed.size() == 2);
    CHECK(keyed[0].frame == kPositionMarker);
    CHECK(keyed[0].at_ms == 900);
    CHECK(keyed[1].frame == kNameMarker);
    CHECK(keyed[1].at_ms == 1050);
    const std::vector<events::RfEvent> done = executor.drained(events::RfEventType::TxDone);
    REQUIRE(done.size() == 2);
    CHECK(done[0].tx_at_us == 900000);
    CHECK(done[1].tx_at_us == 1050000);
}

// Replacing the queued plan was how the name was lost when the position was queued after it.
TEST_CASE("rf: a burst armed for the queued dwell joins it instead of replacing it") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    ports::RfPlan slot0 = Executor::slot1();
    slot0.frequency_hz = adsl::kMBand0Hz;
    slot0.start_us = 400000;
    slot0.end_us = 797000;
    executor.clock.set_milliseconds(400);
    REQUIRE(executor.rf.arm(slot0) == Status::Ok);
    executor.rf.service(400);

    REQUIRE(executor.rf.arm(executor.carrying(executor.name, 1100000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 850000)) == Status::Ok);
    const std::vector<Keyed> keyed = executor.fly(401, 1199);

    REQUIRE(keyed.size() == 2);
    CHECK(keyed[0].frame == kPositionMarker);
    CHECK(keyed[1].frame == kNameMarker);
}

TEST_CASE("rf: a burst armed twice for its dwell goes on air once") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.clock.set_milliseconds(790);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 900000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 900000)) == Status::Ok);

    CHECK(executor.fly(790, 1199).size() == 1);
    CHECK(executor.drained(events::RfEventType::Missed).empty());
}

// A lost burst is reported by the instant it was armed for, so the tape can say which one it was.
TEST_CASE("rf: a dwell that closes on two unfinished bursts reports each lost, by its instant") {
    Executor executor;
    executor.completes = false;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.clock.set_milliseconds(790);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 900000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.carrying(executor.name, 1150000)) == Status::Ok);

    executor.fly(790, 1200);

    const std::vector<events::RfEvent> lost = executor.drained(events::RfEventType::Missed);
    REQUIRE(lost.size() == 2);
    CHECK(lost[0].tx_at_us == 900000);
    CHECK(lost[0].keyed_at_us != 0);
    // The position never finished, so the name behind it never keyed.
    CHECK(lost[1].tx_at_us == 1150000);
    CHECK(lost[1].keyed_at_us == 0);
}

// The silicon executor sleeps until this instant, so a stage point it overslept keyed late.
TEST_CASE("rf: a burst stages its lead before its instant, the next once the first is done") {
    ports::RfBursts bursts{};
    CHECK(bursts.stage_at_us(0, 0, 700) == ports::RfBursts::kNever);
    bursts.add(ports::RfBurst{nullptr, 0, 900000});
    bursts.add(ports::RfBurst{nullptr, 0, 1050000});

    CHECK(bursts.stage_at_us(0, 0, 700) == 899300);
    CHECK(bursts.stage_at_us(1, 0, 700) == ports::RfBursts::kNever);
    CHECK(bursts.stage_at_us(1, 1, 700) == 1049300);
    CHECK(bursts.stage_at_us(2, 2, 700) == ports::RfBursts::kNever);
}

TEST_CASE("rf: a dwell carries two bursts and refuses a third") {
    Executor executor;
    REQUIRE(executor.rf.begin() == Status::Ok);
    executor.clock.set_milliseconds(790);
    REQUIRE(executor.rf.arm(executor.carrying(executor.position, 900000)) == Status::Ok);
    REQUIRE(executor.rf.arm(executor.carrying(executor.name, 1050000)) == Status::Ok);
    CHECK(executor.rf.arm(executor.carrying(executor.name, 1150000)) == Status::WouldBlock);
}

// The name goes out in the tail of +4 and +14, after the position at +0 and the Status at +2.
TEST_CASE("rf: on the ground the name goes out in slot 1's tail, two seconds clear of ours") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    std::strncpy(simulator.product().settings().callsign, "D-KXYZ", go::kCallsignBytes - 1);
    simulator.world().set_fix(true);
    simulator.world().set_speed_kt(0);
    run_on(simulator, past_settling(simulator), 40000);

    const simulator::Air& air = simulator.world().air();
    int names = 0;
    uint64_t position_at_us = 0;
    uint64_t last_at_us = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& record = air.record_at(i);
        const uint8_t payload_type = payload_type_of(record);
        if (payload_type == adsl::Packet::kPayloadTypePosition) position_at_us = record.at_us;
        if (payload_type == adsl::Packet::kPayloadTypeOgnDiagnostics && position_at_us != 0) {
            names++;
            CAPTURE(record.at_us);
            CHECK(record.phase_ms < timing::kSlot1WrapMs);
            CHECK(record.at_us - last_at_us > 2000000);
        }
        if (payload_type != 0) last_at_us = record.at_us;
    }
    CHECK(names >= 3);

    // The tape names the row that carried the name, and dates each row by its own report.
    const radio::Log& log = simulator.product().state().radio_log;
    int rows = 0;
    for (int i = 0; i < log.count(); i++) {
        const radio::Entry& entry = log.newest(i);
        if (entry.verdict != radio::Verdict::Transmitted) continue;
        rows++;
        CAPTURE(entry.at_s);
        const int step_ms = static_cast<int>(simulator::Simulator::kStepMs);
        if (entry.callsign) {
            CHECK(entry.channel == 1);
            CHECK(entry.into_ms <= timing::kSlot1WrapMs + step_ms);
        } else {
            CHECK((entry.into_ms >= adsl::kDirectStartMs || entry.into_ms <= step_ms));
        }
        // one 5 ms host step late at most, a neighbour's instant would be 10 ms or more off
        CHECK(entry.tx_keyed_us < simulator::Simulator::kStepMs * 1000);
    }
    CHECK(rows >= 4);
}
