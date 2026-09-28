// One case per record type, at the service that decides the fact it carries.
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

bool link_action(const std::vector<diag::Record>& records, diag::LinkAction action,
                 diag::Link& out) {
    for (const diag::Record& record : records) {
        diag::Link found{};
        if (!diag::read(record, found) || found.action != action) continue;
        out = found;
        return true;
    }
    return false;
}

// The receiver model doing the talking, rather than the rig pushing solutions past it.
void receiver_seconds(Rig& rig, uint32_t& t, uint32_t seconds) {
    for (uint32_t elapsed = 0; elapsed < seconds * 1000; elapsed += 50) {
        rig.platform.chips().gnss.tick(t);
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        t += 50;
    }
}

}  // namespace

TEST_CASE("diag boot: arming names the build and the parts that produced the corpus") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 3);

    CHECK(count_of(records, diag::Type::Boot) == 1);
    diag::Boot boot{};
    REQUIRE(first_of(records, boot));
    CHECK(boot.capabilities == static_cast<uint32_t>(rig.product.capabilities()));
    CHECK(boot.reset == rig.product.reset_reason());
    CHECK(boot.image_state == dfu::ImageState::Confirmed);
}

TEST_CASE("diag config: arming names the settings every other record was decided under") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    rig.settings().aircraft_type = 9;
    rig.settings().alarm_volume = 4;
    rig.settings().battery_offset_mv = -30;
    rig.settings().battery_offset_manual = true;
    rig.settings().units = go::Units::Metric;
    taxi(rig, t, 3);
    arm(rig, t);
    taxi(rig, t, 2);
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Config) == 1);
    diag::Config config{};
    REQUIRE(first_of(records, config));
    CHECK(config.addr == rig.platform.device_addr());
    CHECK(config.addr_table == settings::kAddrTableSkyblip);
    CHECK(config.aircraft_type == 9);
    CHECK(config.alarm_volume == 4);
    CHECK(config.battery_offset_mv == -30);
    CHECK(config.battery_trim_manual);
    CHECK(config.metric);
    CHECK(config.alarm_enabled);
    CHECK(config.settings_version == go::Settings::kCurrentVersion);
}

TEST_CASE("diag config: arming names the power the radio transmits at, as its executor set it") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 2);

    diag::Config config{};
    REQUIRE(first_of(records, config));
    CHECK(config.tx_power_dbm == parts::sx::kConductedDbm);
    CHECK(config.pa_rated_dbm == parts::sx::kPaConfigHighPowerRatedDbm);
}

TEST_CASE("diag gnss: every solution is recorded with what the sky gave it") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 5);

    CHECK(count_of(records, diag::Type::Gnss) >= 4);
    diag::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.fix_valid);
    CHECK(gnss.sats == 10);
    CHECK(gnss.hdop_e2 == 100);
    CHECK(gnss.stage == gnss::Stage::Fixed);
    CHECK(gnss.pps_locked);
}

TEST_CASE("diag gnss: a solution the receiver refused carries the reason it was refused") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.platform.chips().gnss.fix = false;
    receiver_seconds(rig, t, 5);
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    diag::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.reject == gnss::FixReject::NoSolution);
    CHECK_FALSE(gnss.fix_valid);
}

TEST_CASE("diag gnss: a solution the receiver accepted carries no reject at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    receiver_seconds(rig, t, 5);
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    diag::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.reject == gnss::FixReject::None);
    CHECK(gnss.fix_valid);
}

TEST_CASE("diag pps: one record an edge, and the interval it measured against a second") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 6);

    CHECK(count_of(records, diag::Type::Pps) >= 5);
    CHECK(count_of(records, diag::Type::Pps) <= 8);
    diag::Pps pps{};
    REQUIRE(last_of(records, pps));
    CHECK(pps.locked);
    CHECK(pps.interval_us == 1000000);
    CHECK(pps.error_us == 0);
    CHECK(pps.since_edge_ms < 1000);
}

TEST_CASE("diag pps: a receiver that lost its edge still writes a record a second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.platform.pps().set_locked(false);
    taxi(rig, t, 5);
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Pps) >= 4);
    diag::Pps pps{};
    REQUIRE(last_of(records, pps));
    CHECK_FALSE(pps.locked);
    CHECK(pps.interval_us == 0);
}

TEST_CASE("diag dwell: every dwell the radio arms is a record, band edges and hop alike") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 4);

    CHECK(count_of(records, diag::Type::Dwell) >= 3 * 4);
    bool uplink = false;
    bool slot0 = false;
    bool slot1 = false;
    for (const diag::Record& record : records) {
        diag::Dwell dwell{};
        if (!diag::read(record, dwell)) continue;
        CHECK(dwell.armed);
        uplink = uplink || dwell.freq_hz == timing::kObandHz;
        slot0 = slot0 || dwell.freq_hz == timing::kMband0Hz;
        slot1 = slot1 || dwell.freq_hz == timing::kMband1Hz;
        if (dwell.state == timing::SlotState::Slot1) CHECK(dwell.end_ms == timing::kSlot1End);
    }
    CHECK(uplink);
    CHECK(slot0);
    CHECK(slot1);
}

// The executor's worst-since-boot figures could not date the one 5.9 ms hop the bench saw.
TEST_CASE("diag switch: every dwell change is recorded, with the channel it tuned to") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 4);

    CHECK(count_of(records, diag::Type::Switch) >= 3 * 3);
    bool hop = false;
    bool to_oband = false;
    bool to_mband = false;
    for (const diag::Record& record : records) {
        diag::Switch change{};
        if (!diag::read(record, change)) continue;
        CHECK(change.armed_ahead);
        if (change.kind == diag::SwitchKind::Hop) hop = change.to_hz == timing::kMband1Hz;
        if (change.kind == diag::SwitchKind::ToOband) to_oband = change.to_hz == timing::kObandHz;
        if (change.kind == diag::SwitchKind::ToMband) to_mband = change.to_hz == timing::kMband0Hz;
    }
    CHECK(hop);
    CHECK(to_oband);
    CHECK(to_mband);
}

TEST_CASE("diag baro: a sample carries the altitude and the rate taken from it") {
    Rig rig;
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 6);

    CHECK(count_of(records, diag::Type::Baro) >= 4);
    diag::Baro baro{};
    REQUIRE(last_of(records, baro));
    CHECK(baro.pressure_mpa > 0);
    CHECK(baro.active);
    CHECK(baro.alt_mm != 0);
}

TEST_CASE("diag motion: the hub's axes are recorded on their own cadence") {
    Rig rig;
    uint32_t t = 100;
    const int seconds = 6;
    const std::vector<diag::Record> records = armed_taxi(rig, t, seconds);

    CHECK(count_of(records, diag::Type::Motion) >= seconds - 1);
    CHECK(count_of(records, diag::Type::Motion) <= seconds + 1);
    diag::Motion motion{};
    REQUIRE(last_of(records, motion));
    CHECK(motion.fitted);
    CHECK(motion.imu_error == rig.state().imu.error);
}

TEST_CASE("diag motion: a unit with no hub writes no motion record at all") {
    constexpr ports::Capabilities kNoHub = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Inclinometer));
    Rig rig{kNoHub};
    uint32_t t = 100;
    const std::vector<diag::Record> records = armed_taxi(rig, t, 4);

    CHECK(count_of(records, diag::Type::Motion) == 0);
    CHECK(count_of(records, diag::Type::Power) > 0);
}

TEST_CASE("diag contact: both edges of a press are recorded with what the product made of them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.press(t);
    rig.run(t, t + 200);
    t += 200;
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Contact) == 2);
    diag::Contact down{};
    REQUIRE(first_of(records, down));
    CHECK(down.contact == events::Contact::Button);
    CHECK(down.down);
    CHECK(down.gesture == static_cast<uint8_t>(go::Gesture::None));
    diag::Contact up{};
    REQUIRE(last_of(records, up));
    CHECK_FALSE(up.down);
    CHECK(up.gesture == static_cast<uint8_t>(go::Gesture::Press));
    CHECK(up.held_ms == up.at_ms - down.at_ms);
}

TEST_CASE("diag link: a central connecting is a record, with the payload it negotiated") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.raise_link(7);
    rig.run(t, t + 200);
    t += 200;
    rig.send("{\"cmd\":\"status\"}");
    rig.run(t, t + 200);
    t += 200;
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    diag::Link up{};
    REQUIRE(link_action(records, diag::LinkAction::Up, up));
    CHECK(up.session == 7);
    CHECK(up.payload_bytes == rig.platform.link().payload_bytes());
    diag::Link received{};
    REQUIRE(link_action(records, diag::LinkAction::Received, received));
    CHECK(received.endpoint == events::Endpoint::Config);
    CHECK(received.session == 7);
    CHECK(received.frame_bytes > 0);
    diag::Link claimed{};
    REQUIRE(link_action(records, diag::LinkAction::ClaimTaken, claimed));
    CHECK(claimed.claim_held);
    CHECK(claimed.holder == 7);
}

TEST_CASE("diag traffic: an aircraft is recorded a reception, with the margin it was graded on") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);

    for (int i = 0; i < 4; i++) {
        model::AircraftObs obs{};
        obs.addr = 0x515151;
        obs.addr_table = 6;
        obs.position_valid = true;
        obs.speed_valid = true;
        obs.speed_q = 40 * 4;
        obs.alt_m = 300;
        obs.lat_1e7 = rig.state().own.lat_1e7 + 9000;
        obs.lon_1e7 = rig.state().own.lon_1e7;
        obs.at_ms = t;
        obs.rssi_dbm = -92;
        obs.source = model::Source::AdslDirect;
        obs.received.at_s = rig.state().traffic_now(t);
        rig.state().traffic.update(obs, rig.state().traffic_now(t));
        taxi(rig, t, 1);
    }
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Traffic) >= 3);
    diag::Traffic seen{};
    REQUIRE(last_of(records, seen));
    CHECK(seen.addr == 0x515151);
    CHECK(seen.source == model::Source::AdslDirect);
    CHECK(seen.rssi_dbm == -92);
    CHECK(seen.tracked == 1);
    CHECK(seen.assessed);
    CHECK(seen.dist_m > 0);
    CHECK(seen.dist_m < 2000);
}

TEST_CASE("diag write: a pending settings write is recorded every evaluation, not only the write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm(rig, t);
    rig.settings().alarm_volume = 5;
    rig.product.config().config().note_settings_changed();
    taxi(rig, t, 6);
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Write) > 1);
    bool placed = false;
    for (const diag::Record& record : records) {
        diag::Write write{};
        if (!diag::read(record, write)) continue;
        CHECK(write.kind == power::DurableWrite::Settings);
        CHECK(write.placement != timing::DurableWriteVerdict::Idle);
        placed = placed || write.placement == timing::DurableWriteVerdict::Place;
        if (write.placement == timing::DurableWriteVerdict::Place)
            CHECK(write.waited_ms >= timing::DurableWriteWindow::kSettleMs);
    }
    CHECK(placed);
}

TEST_CASE("diag screen: what was on the glass is recorded on the render cadence") {
    Rig rig;
    uint32_t t = 100;
    const int seconds = 8;
    const std::vector<diag::Record> records = armed_taxi(rig, t, seconds);

    CHECK(count_of(records, diag::Type::Screen) >= seconds - 1);
    CHECK(count_of(records, diag::Type::Screen) <= seconds + 1);
    diag::Screen screen{};
    REQUIRE(last_of(records, screen));
    CHECK(screen.page == static_cast<uint8_t>(rig.product.screen().page()));
    CHECK(screen.mode == static_cast<uint8_t>(go::Mode::Page));
    CHECK(screen.powered);
    CHECK_FALSE(screen.holding);
    CHECK(screen.since_ms > 0);
}

// The page turns this into the hours a partition lasts, so it is a claim about the taps.
TEST_CASE("diag: the rate the capture page quotes covers a quiet airborne second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 8);
    arm(rig, t);
    const uint32_t before = rig.product.capture().records_written();
    const uint32_t seconds = 20;
    rig.seconds(t, seconds, 50000, 800);
    const uint32_t measured = (rig.product.capture().records_written() - before) / seconds;
    MESSAGE("airborne, empty sky: " << measured << " records a second");
    CHECK(measured <= diag::Recorder::kPeriodicRecordsPerSecond);
    CHECK(measured + 2 >= diag::Recorder::kPeriodicRecordsPerSecond);
}

// The one fact every tap shares: disarmed, the arguments may be gathered but nothing is kept.
TEST_CASE("diag: a device nobody armed records nothing at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    rig.raise_link(3);
    rig.press(t);
    rig.settings().alarm_volume = 2;
    rig.product.config().config().note_settings_changed();
    taxi(rig, t, 6);

    CHECK(rig.product.diag().written() == 0);
    CHECK(rig.product.diag().queued() == 0);
    CHECK(rig.product.capture().records_written() == 0);
    CHECK(captured(rig).empty());
}
