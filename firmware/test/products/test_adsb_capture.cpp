// The ADS-B record a Bravo or a paired Go writes for each aircraft it folds, and how often.
#include <map>
#include <string>
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/units/units.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "ports/null.h"
#include "products/common/services/adsb_capture.h"
#include "test/support/adsb_frames.h"
#include "test/support/adsb_receiver.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/diagnostics_taps.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

using namespace skyblip;

namespace {

struct Writer {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    runtime::Context context;
    common::AdsbCapture capture{context};

    Writer(bus::Bus& bus, bus::State& state, diagnostics::Recorder& recorder)
        : context{roles, bus, state, recorder} {}
};

struct Bench {
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    Writer writer{bus, state, recorder};

    Bench() { recorder.arm(); }
};

model::AircraftObservation heard(uint32_t address, uint32_t at_ms) {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.source = model::Source::Adsb;
    observation.at_ms = at_ms;
    return observation;
}

}  // namespace

TEST_CASE("adsb capture: one record per aircraft each span a GNSS-baro difference is trusted") {
    Bench bench;
    common::AdsbCapture& capture = bench.writer.capture;

    CHECK(capture.record(heard(0x3C4D5E, 1000), 1000));
    CHECK_FALSE(capture.record(heard(0x3C4D5E, 1500), 1500));
    CHECK(capture.record(heard(0x4CA2B1, 1500), 1500));
    CHECK_FALSE(
        capture.record(heard(0x3C4D5E, 10999), 1000 + common::AdsbCapture::kRecordPeriodMs - 1));
    CHECK(capture.record(heard(0x3C4D5E, 11000), 1000 + common::AdsbCapture::kRecordPeriodMs));
    CHECK(bench.recorder.queued() == 3);
}

TEST_CASE("adsb capture: nothing is recorded while disarmed, nor for a target ADS-B did not send") {
    Bench bench;
    model::AircraftObservation direct = heard(0x3C4D5E, 1000);
    direct.source = model::Source::AdslDirect;
    CHECK_FALSE(bench.writer.capture.record(direct, 1000));

    bench.recorder.disarm();
    CHECK_FALSE(bench.writer.capture.record(heard(0x3C4D5E, 1000), 1000));
    bench.recorder.arm();
    CHECK(bench.writer.capture.record(heard(0x3C4D5E, 1000), 1000));
}

// 101 aircraft through a 100-aircraft assembly, round-robin every 10 ms, never record faster.
TEST_CASE(
    "adsb capture: a sky that churns past the assembly never records faster than a full one") {
    using namespace skyblip::adsb_receiver;
    Bench bench;
    Bravo bravo;
    constexpr uint32_t kAircraft = 101;
    std::map<uint32_t, uint32_t> last_ms;
    std::vector<uint32_t> accepted_ms;
    for (uint32_t at_ms = 0; at_ms <= 10100; at_ms += 10) {
        const uint32_t address = 0x100000 + (at_ms / 10) % kAircraft;
        if (!bravo.hear(adsb_airborne_position(address, kNoCapability, kLatitude, kLongitude,
                                               (at_ms / 10) % 2 == 1, 3000),
                        at_ms))
            continue;
        if (!bench.writer.capture.record(bravo.heard, at_ms)) continue;
        bench.recorder.commit();
        if (last_ms.count(address) != 0)
            CHECK(at_ms - last_ms[address] >= common::AdsbCapture::kRecordPeriodMs);
        last_ms[address] = at_ms;
        accepted_ms.push_back(at_ms);
    }
    CHECK(bench.writer.capture.refused() > 0);
    for (size_t first = 0; first < accepted_ms.size(); first++) {
        size_t in_period = 0;
        for (size_t later = first; later < accepted_ms.size(); later++)
            if (accepted_ms[later] - accepted_ms[first] < common::AdsbCapture::kRecordPeriodMs)
                in_period++;
        CHECK(in_period <= static_cast<size_t>(common::AdsbCapture::kAircraft));
    }
}

TEST_CASE(
    "adsb capture: an aircraft past the second's budget is refused, counted, and let in later") {
    Bench bench;
    common::AdsbCapture& capture = bench.writer.capture;
    for (uint32_t i = 0; i < common::AdsbCapture::kBudgetCeilingRecords; i++) {
        REQUIRE(capture.record(heard(0x100000 + i, 0), i));
        bench.recorder.commit();
    }
    CHECK_FALSE(capture.record(heard(0x200000, 0), 500));
    CHECK(capture.refused() == 1);
    CHECK_FALSE(capture.record(heard(0x100000, 999), 999));
    CHECK(capture.refused() == 1);
    CHECK(capture.record(heard(0x200000, 1000), common::AdsbCapture::kBudgetWindowMs));
}

// A capture armed under a full sky would write every aircraft on its first fold, at once.
TEST_CASE("adsb capture: a full sky at arming spreads its first records over the period") {
    Bench bench;
    constexpr uint32_t kSpreadMs = 1000;
    constexpr uint32_t kPerSpread = static_cast<uint32_t>(common::AdsbCapture::kAircraft) *
                                    kSpreadMs / common::AdsbCapture::kRecordPeriodMs;
    std::vector<uint32_t> accepted_ms;
    for (uint32_t at_ms = 0; at_ms < common::AdsbCapture::kRecordPeriodMs; at_ms += 100)
        for (uint32_t i = 0; i < static_cast<uint32_t>(common::AdsbCapture::kAircraft); i++)
            if (bench.writer.capture.record(heard(0x100000 + i, at_ms), at_ms)) {
                bench.recorder.commit();
                accepted_ms.push_back(at_ms);
            }
    CHECK(accepted_ms.size() == static_cast<size_t>(common::AdsbCapture::kAircraft));
    for (size_t first = 0; first < accepted_ms.size(); first++) {
        uint32_t in_spread = 0;
        for (size_t later = first; later < accepted_ms.size(); later++)
            if (accepted_ms[later] - accepted_ms[first] < kSpreadMs) in_spread++;
        CHECK(in_spread <= kPerSpread);
    }
}

TEST_CASE("adsb capture: an aircraft whose record the ring refused is recorded on its next fold") {
    Bench bench;
    while (bench.recorder.record(diagnostics::record_of(diagnostics::End{}, {}))) {
    }
    CHECK_FALSE(bench.writer.capture.record(heard(0x3C4D5E, 1000), 1000));
    bench.recorder.commit();
    bench.recorder.commit();
    CHECK(bench.writer.capture.record(heard(0x3C4D5E, 1010), 1010));
}

// scripts/test_blip_altitude.py decodes these bytes, read off the rig's flash.
TEST_CASE("adsb capture: DF17 frames folded, recorded, drained to flash and read back") {
    using namespace skyblip::adsb_receiver;
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);

    Bravo bravo;
    const uint32_t at_ms = now_ms;
    REQUIRE(
        bravo.hear(adsb_ground_velocity(kAddress, kNoCapability, 0, 80, 0, -275), at_ms - 1200));
    REQUIRE(bravo.hear(adsb_operational_status(kAddress, kNoCapability, 2, 2), at_ms - 1100));
    bravo.fly(kLatitude, kLongitude, at_ms - 700, false, 3000);
    bravo.fly(kLatitude, kLongitude, at_ms - 200, true, 3000);
    REQUIRE(bravo.heard.altitude_valid);

    model::Ownship& own = rig.state().own;
    own.pressure_altitude_mm = 280000;
    own.pressure_altitude_at_ms = at_ms;
    own.pressure_altitude_valid = true;
    const int32_t own_hae_m = to_metres(Millimetres(own.altitude_hae_mm)).value;

    Writer writer{rig.product.bus(), rig.state(), rig.product.diagnostics()};
    REQUIRE(writer.capture.record(bravo.heard, at_ms));
    stop(rig, now_ms);

    const std::vector<diagnostics::Record> records = captured(rig);
    REQUIRE(count_of(records, diagnostics::Type::Adsb) == 1);
    diagnostics::Adsb back{};
    REQUIRE(first_of(records, back));
    CHECK(back.address == kAddress);
    CHECK(back.adsb_version_valid);
    CHECK(back.adsb_version == 2);
    CHECK(back.gva_valid);
    CHECK(back.gva == 2);
    CHECK(back.pressure_altitude_ft == 3000);
    CHECK(back.height_difference_ft == -275);
    CHECK(back.height_difference_age_ms == 1200);
    CHECK(back.gnss_relative_altitude_m == to_metres(Feet(2725)).value - own_hae_m);
    // 3000 ft is 914.4 m, against own 280 m.
    CHECK(back.pressure_relative_altitude_m == 634);

    uint8_t raw[diagnostics::kRecordBytes]{};
    for (const diagnostics::Record& record : records)
        if (record.type == diagnostics::Type::Adsb) diagnostics::encode_record(record, raw);
    char text[2 * diagnostics::kRecordBytes + 1]{};
    hex::write(ConstByteSpan(raw, sizeof(raw)), text);
    CHECK(std::string(text) == "15ffc80083886e6a5e4d3c12b80b0000edfeb004e5017a02");
}
