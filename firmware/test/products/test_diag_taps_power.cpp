// The power and duty records: the cell on its cadence, the knee, the trim, and the duty pair.
#include <vector>

#include "core/bus/state.h"
#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/screen.h"
#include "test/support/diag_corpus.h"
#include "test/support/diag_taps.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// A Duty record whose Power is the one written on the same pass, under the same instant.
int paired_with_power(const std::vector<diag::Record>& records) {
    int paired = 0;
    for (size_t i = 1; i < records.size(); i++) {
        if (records[i].type != diag::Type::Duty) continue;
        if (records[i - 1].type != diag::Type::Power) continue;
        if (records[i - 1].at_s != records[i].at_s) continue;
        if (records[i - 1].into_ms != records[i].into_ms) continue;
        paired++;
    }
    return paired;
}

std::vector<uint32_t> duty_spacing_ms(const std::vector<diag::Record>& records) {
    std::vector<uint32_t> out;
    const diag::Record* previous = nullptr;
    for (const diag::Record& record : records) {
        if (record.type != diag::Type::Duty) continue;
        if (previous != nullptr)
            out.push_back((record.at_s - previous->at_s) * 1000 + record.into_ms -
                          previous->into_ms);
        previous = &record;
    }
    return out;
}

std::vector<diag::Power> every_power_record(const std::vector<diag::Record>& records) {
    std::vector<diag::Power> out;
    for (const diag::Record& record : records) {
        diag::Power power{};
        if (diag::read(record, power)) out.push_back(power);
    }
    return out;
}

}  // namespace

TEST_CASE("diag power: the cell is recorded on the cadence it is sampled at") {
    Rig rig;
    uint32_t t = 100;
    const int seconds = 8;
    const std::vector<diag::Record> records = armed_taxi(rig, t, seconds);

    CHECK(count_of(records, diag::Type::Power) >= seconds - 1);
    CHECK(count_of(records, diag::Type::Power) <= seconds + 1);
    diag::Power power{};
    REQUIRE(last_of(records, power));
    CHECK(power.valid);
    CHECK(power.cell_mv > 3000);
    CHECK(power.level == rig.state().power.level);
}

TEST_CASE("diag power: the knee is recorded on both sides of it, and the level never moves") {
    Rig rig;
    uint32_t t = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 3800;
    taxi(rig, t, 3);
    arm(rig, t);
    taxi(rig, t, 4);
    rig.platform.battery().millivolts = 3550;
    taxi(rig, t, 6);
    stop(rig, t);

    const std::vector<diag::Power> cell = every_power_record(captured(rig));
    REQUIRE(cell.size() >= 8);
    CHECK_FALSE(cell.front().caution);
    CHECK(cell.front().cell_mv == 3800);
    CHECK(cell.back().caution);
    CHECK(cell.back().cell_mv == 3550);
    CHECK(cell.back().caution == rig.state().power.caution);
    for (const diag::Power& power : cell) CHECK(power.level == power::PowerLevel::Normal);
}

// The median kept the gauge still through a sag, and the corpus could not see the sag at all.
TEST_CASE("diag power: the reading the median threw out rides beside it, to the millivolt") {
    Rig rig;
    uint32_t t = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().millivolts = 3800;
    taxi(rig, t, 3);
    arm(rig, t);
    taxi(rig, t, 4);
    rig.platform.battery().millivolts = 3737;
    taxi(rig, t, 1);
    rig.platform.battery().millivolts = 3800;
    taxi(rig, t, 3);
    rig.platform.battery().millivolts = 3500;
    taxi(rig, t, 1);
    rig.platform.battery().millivolts = 3800;
    taxi(rig, t, 3);
    stop(rig, t);

    bool dip_seen = false, sag_seen = false;
    for (const diag::Power& power : every_power_record(captured(rig))) {
        CHECK(power.cell_mv == 3800);
        dip_seen = dip_seen || power.sample_offset_mv == -63;
        sag_seen = sag_seen || power.sample_offset_mv == diag::kSampleOffsetFloorMv;
    }
    CHECK(dip_seen);
    // 300 mV under the median saturates: the record says at least that far
    CHECK(sag_seen);
}

// Two minutes on a charger holding its float voltage, which is the only bench this unit gets.
TEST_CASE("diag power: a trim the charger taught the unit reaches the corpus, not only settings") {
    Rig rig;
    uint32_t t = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().external_power = true;
    rig.platform.battery().millivolts = 3960;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.run(t, t + 10000);
    t += 10000;
    rig.platform.battery().millivolts = 4160;
    rig.run(t, t + power::kPlateauHoldMs + 5000);
    t += power::kPlateauHoldMs + 5000;
    stop(rig, t);

    const std::vector<diag::Power> cell = every_power_record(captured(rig));
    REQUIRE_FALSE(cell.empty());
    CHECK(cell.back().trim_learned);
    CHECK(cell.back().trim_offset_mv == 40);
    CHECK(cell.back().trim_offset_mv == rig.settings().battery_offset_mv);
}

// A reader divides a duty delta by a power delta: interpolating between two instants is not that.
TEST_CASE("diag duty: a duty record rides a power pass, on a slower cadence of its own") {
    Rig rig;
    uint32_t t = 100;
    const int seconds = 22;
    const std::vector<diag::Record> records = armed_taxi(rig, t, seconds);

    CHECK(count_of(records, diag::Type::Power) >= seconds - 1);
    const int duty = count_of(records, diag::Type::Duty);
    // 22 s of capture at one duty record every 10 s
    CHECK(duty >= 2);
    CHECK(duty <= 3);
    CHECK(paired_with_power(records) == duty);
}

TEST_CASE("diag duty: a power run writes the pair every 30 s and lists nothing else") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t, diag::Profile::PowerRun);
    rig.run(t, t + 95000);
    t += 95000;
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    // pairs land about 30, 60 and 90 s after arming, and the pilot's stop at 95 s writes none
    CHECK(count_of(records, diag::Type::Duty) == 3);
    CHECK(count_of(records, diag::Type::Power) == 3);
    CHECK(paired_with_power(records) == 3);
    const std::vector<uint32_t> spacing = duty_spacing_ms(records);
    REQUIRE(spacing.size() == 2);
    CHECK(spacing[0] == diag::kPowerRunRecordPeriodMs);
    CHECK(spacing[1] == diag::kPowerRunRecordPeriodMs);
    CHECK(count_of(records, diag::Type::Gnss) == 0);
    CHECK(count_of(records, diag::Type::Dwell) == 0);
}

TEST_CASE("diag duty: the record carries what each service published, not a count of its own") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    rig.raise_link();
    rig.product.screen().set_backlight(true);
    arm(rig, t);
    taxi(rig, t, 12);
    const bus::DutyState published = rig.state().duty;
    stop(rig, t);

    diag::Duty duty{};
    REQUIRE(last_of(captured(rig), duty));
    CHECK(duty.panel_partial_refreshes > 0);
    CHECK(duty.panel_partial_refreshes <= published.panel_partial_refreshes);
    CHECK(duty.backlight_ms > 0);
    CHECK(duty.backlight_ms <= published.backlight_ms);
    CHECK(duty.rx_armed_ms > 0);
    CHECK(duty.rx_armed_ms <= published.rx_armed_ms);
    CHECK(duty.ble_connected_ms > 0);
    CHECK(duty.ble_connected_ms <= published.ble_connected_ms);
    CHECK(duty.panel_full_refreshes == published.panel_full_refreshes);
    CHECK(duty.annunciator_ms == published.annunciator_ms);
    CHECK(duty.tx_keyed_ms == published.tx_keyed_ms);
}
