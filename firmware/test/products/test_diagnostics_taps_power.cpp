// The power and duty records: the cell on its cadence, the knee, the cable, and the duty pair.
#include <vector>

#include "core/bus/state.h"
#include "core/diagnostics/payload.h"
#include "core/diagnostics/profile.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/screen.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/diagnostics_taps.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// A Duty record whose Power is the one written on the same pass, under the same instant.
int paired_with_power(const std::vector<diagnostics::Record>& records) {
    int paired = 0;
    for (size_t i = 1; i < records.size(); i++) {
        if (records[i].type != diagnostics::Type::Duty) continue;
        if (records[i - 1].type != diagnostics::Type::Power) continue;
        if (records[i - 1].at_s != records[i].at_s) continue;
        if (records[i - 1].into_ms != records[i].into_ms) continue;
        paired++;
    }
    return paired;
}

std::vector<uint32_t> duty_spacing_ms(const std::vector<diagnostics::Record>& records) {
    std::vector<uint32_t> out;
    const diagnostics::Record* previous = nullptr;
    for (const diagnostics::Record& record : records) {
        if (record.type != diagnostics::Type::Duty) continue;
        if (previous != nullptr)
            out.push_back((record.at_s - previous->at_s) * 1000 + record.into_ms -
                          previous->into_ms);
        previous = &record;
    }
    return out;
}

std::vector<diagnostics::Power> every_power_record(
    const std::vector<diagnostics::Record>& records) {
    std::vector<diagnostics::Power> out;
    for (const diagnostics::Record& record : records) {
        diagnostics::Power power{};
        if (diagnostics::read(record, power)) out.push_back(power);
    }
    return out;
}

}  // namespace

TEST_CASE("diagnostics power: the cell is recorded on the cadence it is sampled at") {
    Rig rig;
    uint32_t now_ms = 100;
    const int seconds = 8;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, seconds);

    CHECK(count_of(records, diagnostics::Type::Power) >= seconds - 1);
    CHECK(count_of(records, diagnostics::Type::Power) <= seconds + 1);
    diagnostics::Power power{};
    REQUIRE(last_of(records, power));
    CHECK(power.valid);
    CHECK(power.cell_mv > 3000);
    CHECK(power.level == rig.state().power.level);
}

TEST_CASE("diagnostics power: the knee is recorded on both sides of it, as the level") {
    Rig rig;
    uint32_t now_ms = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().divider_mv = 3800;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    taxi(rig, now_ms, 4);
    rig.platform.battery().divider_mv = 3550;
    taxi(rig, now_ms, 6);
    stop(rig, now_ms);

    const std::vector<diagnostics::Power> cell = every_power_record(captured(rig));
    REQUIRE(cell.size() >= 8);
    CHECK(cell.front().level == power::PowerLevel::Normal);
    CHECK(cell.front().cell_mv == 3800);
    CHECK(cell.back().level == power::PowerLevel::Low);
    CHECK(cell.back().cell_mv == 3550);
    CHECK(cell.back().level == rig.state().power.level);
}

// The median kept the gauge still through a sag, and the corpus could not see the sag at all.
TEST_CASE("diagnostics power: the reading the median threw out rides beside it, to the millivolt") {
    Rig rig;
    uint32_t now_ms = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().divider_mv = 3800;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    taxi(rig, now_ms, 4);
    rig.platform.battery().divider_mv = 3737;
    taxi(rig, now_ms, 1);
    rig.platform.battery().divider_mv = 3800;
    taxi(rig, now_ms, 3);
    rig.platform.battery().divider_mv = 3500;
    taxi(rig, now_ms, 1);
    rig.platform.battery().divider_mv = 3800;
    taxi(rig, now_ms, 3);
    stop(rig, now_ms);

    bool dip_seen = false, sag_seen = false;
    for (const diagnostics::Power& power : every_power_record(captured(rig))) {
        CHECK(power.cell_mv == 3800);
        dip_seen = dip_seen || power.sample_offset_mv == -63;
        sag_seen = sag_seen || power.sample_offset_mv == diagnostics::kSampleOffsetFloorMv;
    }
    CHECK(dip_seen);
    // 300 mV under the median saturates: the record says at least that far
    CHECK(sag_seen);
}

// 0B1B2C charged for 7 h with every record reading no cable and no cell (#114).
TEST_CASE(
    "diagnostics power: on the cable the record carries the cable and what the divider read") {
    Rig rig;
    uint32_t now_ms = 100;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().external_power = true;
    rig.platform.battery().divider_mv = 4812;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    taxi(rig, now_ms, 4);
    rig.platform.battery().fitted = false;
    taxi(rig, now_ms, 3);
    stop(rig, now_ms);

    const std::vector<diagnostics::Power> cell = every_power_record(captured(rig));
    REQUIRE(cell.size() >= 6);
    for (const diagnostics::Power& power : cell) {
        CHECK(power.external_power);
        CHECK(power.charging);
        CHECK_FALSE(power.valid);
        CHECK(power.level == power::PowerLevel::Normal);
    }
    CHECK(cell.front().cell_mv == 4812);
    CHECK(cell.front().implausible >= 4);
    CHECK(cell.back().cell_mv == 0);
    CHECK(cell.back().implausible > cell.front().implausible);
}

// A reader divides a duty delta by a power delta: interpolating between two instants is not that.
TEST_CASE("diagnostics duty: a duty record rides a power pass, on a slower cadence of its own") {
    Rig rig;
    uint32_t now_ms = 100;
    const int seconds = 22;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, seconds);

    CHECK(count_of(records, diagnostics::Type::Power) >= seconds - 1);
    const int duty = count_of(records, diagnostics::Type::Duty);
    // 22 s of capture at one duty record every 10 s
    CHECK(duty >= 2);
    CHECK(duty <= 3);
    CHECK(paired_with_power(records) == duty);
}

TEST_CASE("diagnostics duty: a power run writes the pair every 30 s and lists nothing else") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms, diagnostics::Profile::PowerRun);
    rig.run(now_ms, now_ms + 95000);
    now_ms += 95000;
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    // pairs land about 30, 60 and 90 s after arming, and the pilot's stop at 95 s writes none
    CHECK(count_of(records, diagnostics::Type::Duty) == 3);
    CHECK(count_of(records, diagnostics::Type::Power) == 3);
    CHECK(paired_with_power(records) == 3);
    const std::vector<uint32_t> spacing = duty_spacing_ms(records);
    REQUIRE(spacing.size() == 2);
    CHECK(spacing[0] == diagnostics::kPowerRunRecordPeriodMs);
    CHECK(spacing[1] == diagnostics::kPowerRunRecordPeriodMs);
    CHECK(count_of(records, diagnostics::Type::Gnss) == 0);
    CHECK(count_of(records, diagnostics::Type::Dwell) == 0);
}

TEST_CASE(
    "diagnostics duty: the record carries what each service published, not a count of its own") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    rig.raise_link();
    rig.product.screen().set_backlight(true);
    arm(rig, now_ms);
    taxi(rig, now_ms, 12);
    const bus::DutyState published = rig.state().duty;
    stop(rig, now_ms);

    diagnostics::Duty duty{};
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
