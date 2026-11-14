// The reception trial's schedule, and the records a slot and a sampled frame leave in a capture.
#include <vector>

#include "core/diagnostics/adsb_trial.h"
#include "core/diagnostics/recorder.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "ports/null.h"
#include "products/skyblip_bravo/services/adsb_trial.h"

using namespace skyblip;

namespace {

constexpr uint32_t kSlotS = 10;
constexpr uint32_t kSlotMs = kSlotS * 1000;

struct Bench {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    bravo::AdsbTrial trial{context};
    ports::AdsbReceiverStatistics receiver{};

    void start(int count) {
        comms::AdsbTrialPlan plan{};
        plan.count = count;
        plan.slot_s = kSlotS;
        for (int i = 0; i < count; i++)
            plan.profiles[static_cast<size_t>(i)].threshold_dbm = static_cast<int16_t>(-90 - i);
        trial.start(plan, 0, receiver);
    }

    std::vector<diagnostics::Record> records() {
        std::vector<diagnostics::Record> out(diagnostics::Recorder::kMaxRecords);
        out.resize(static_cast<size_t>(recorder.peek(out.data(), static_cast<int>(out.size()))));
        return out;
    }

    template <class T>
    std::vector<T> read_all() {
        std::vector<T> out;
        for (const diagnostics::Record& record : records()) {
            T value{};
            if (diagnostics::read(record, value)) out.push_back(value);
        }
        return out;
    }
};

events::AdsbFrame frame(uint8_t first_chip) {
    events::AdsbFrame out{};
    for (size_t i = 0; i < out.chips.size(); i++)
        out.chips[i] = static_cast<uint8_t>(first_chip + i);
    out.rssi_dbm = -80;
    out.rssi_valid = true;
    return out;
}

model::AircraftObservation folded(uint32_t address, bool position) {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.position_valid = position;
    return observation;
}

}  // namespace

TEST_CASE("adsb trial: profiles take turns slot by slot, and the order turns back each cycle") {
    Bench bench;
    bench.start(2);
    std::vector<int> thresholds;
    for (uint32_t slot = 0; slot < 6; slot++) {
        const auto tuning = bench.trial.tick(slot * kSlotMs, bench.receiver);
        REQUIRE(tuning.has_value());
        thresholds.push_back(tuning->detection_threshold_dbm);
        CHECK_FALSE(bench.trial.tick(slot * kSlotMs + 10, bench.receiver).has_value());
    }
    CHECK(thresholds == std::vector<int>{-90, -91, -91, -90, -90, -91});
}

TEST_CASE("adsb trial: a slot counts nothing until the retune settled, then closes into a record") {
    Bench bench;
    bench.recorder.arm();
    bench.start(1);
    REQUIRE(bench.trial.tick(0, bench.receiver).has_value());
    bench.trial.heard(frame(0), Status::Crc, 500);
    CHECK_FALSE(bench.trial.counting());

    bench.trial.tick(bravo::AdsbTrial::kRetuneSettleMs, bench.receiver);
    REQUIRE(bench.trial.counting());
    bench.trial.heard(frame(0), Status::Ok, 2000);
    bench.trial.heard(frame(0), Status::Crc, 2000);
    bench.trial.heard(frame(0), Status::Unsupported, 2000);
    bench.trial.folded(folded(0x3C4D5E, true));
    bench.trial.folded(folded(0x3C4D5E, false));
    bench.trial.folded(folded(0x4840D6, true));
    bench.receiver.dropped = 1;
    bench.trial.tick(kSlotMs, bench.receiver);

    const std::vector<diagnostics::AdsbSlot> slots = bench.read_all<diagnostics::AdsbSlot>();
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].threshold_dbm == -90);
    CHECK(slots[0].frames == 3);
    CHECK(slots[0].crc == 1);
    CHECK(slots[0].unsupported == 1);
    CHECK(slots[0].folded == 3);
    CHECK(slots[0].positions == 2);
    CHECK(slots[0].aircraft == 2);
    CHECK(slots[0].counted_s == 9);
    CHECK(slots[0].front_end_raw);
    CHECK(slots[0].fifo_dropped);
}

TEST_CASE("adsb trial: four frames a second are sampled, each as a head and a tail of chips") {
    Bench bench;
    bench.recorder.arm();
    bench.start(1);
    bench.trial.tick(0, bench.receiver);
    bench.trial.tick(bravo::AdsbTrial::kRetuneSettleMs, bench.receiver);
    for (int i = 0; i < 6; i++) bench.trial.heard(frame(0x10), Status::Crc, 2100);

    const std::vector<diagnostics::AdsbChips> halves = bench.read_all<diagnostics::AdsbChips>();
    REQUIRE(halves.size() == 2 * bravo::AdsbTrial::kChipsFramesPerSecond);
    CHECK_FALSE(halves[0].tail);
    CHECK(halves[1].tail);
    CHECK(halves[0].seq == halves[1].seq);
    CHECK(halves[0].verdict == diagnostics::AdsbChipsVerdict::Crc);
    CHECK(halves[0].rssi_valid);
    CHECK(halves[0].rssi_dbm == -80);
    CHECK(halves[0].chips[0] == 0x10);
    CHECK(halves[1].chips[0] == 0x10 + diagnostics::kAdsbChipsPerRecord);
    // 27 chip bytes: 14 in the head, 13 in the tail, and the tail's last byte left at 0.
    CHECK(halves[1].chips[12] == 0x10 + 26);
    CHECK(halves[1].chips[13] == 0);

    bench.trial.heard(frame(0x10), Status::Crc, 3000);
    CHECK(bench.read_all<diagnostics::AdsbChips>().size() == halves.size() + 2);
}

TEST_CASE("adsb trial: a stop closes the slot it ran and puts the default tuning back") {
    Bench bench;
    bench.recorder.arm();
    bench.start(2);
    bench.trial.tick(0, bench.receiver);
    bench.trial.tick(2000, bench.receiver);

    bench.trial.start(comms::AdsbTrialPlan{}, 4000, bench.receiver);
    const auto tuning = bench.trial.tick(4010, bench.receiver);
    REQUIRE(tuning.has_value());
    CHECK(tuning->detection_threshold_dbm == parts::lr2021::kDetectionThresholdDbm);
    CHECK(tuning->layout == parts::Lr2021Layout::Chips);
    CHECK_FALSE(bench.trial.active());
    const std::vector<diagnostics::AdsbSlot> slots = bench.read_all<diagnostics::AdsbSlot>();
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].counted_s == 3);
}
