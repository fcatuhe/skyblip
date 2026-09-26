// One case per record type, at the service that decides the fact it carries.
#include <vector>

#include "core/bus/state.h"
#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/power.h"
#include "products/skyblip_go/services/screen.h"
#include "test/support/diag_corpus.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

int count_of(const std::vector<diag::Record>& records, diag::Type type) {
    int n = 0;
    for (const diag::Record& record : records)
        if (record.type == type) n++;
    return n;
}

template <class T>
bool first_of(const std::vector<diag::Record>& records, T& out) {
    for (const diag::Record& record : records)
        if (diag::read(record, out)) return true;
    return false;
}

template <class T>
bool last_of(const std::vector<diag::Record>& records, T& out) {
    bool found = false;
    for (const diag::Record& record : records) found = diag::read(record, out) || found;
    return found;
}

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

std::vector<diag::Power> every_power_record(const std::vector<diag::Record>& records) {
    std::vector<diag::Power> out;
    for (const diag::Record& record : records) {
        diag::Power power{};
        if (diag::read(record, power)) out.push_back(power);
    }
    return out;
}

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

void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }

void arm(Rig& rig, uint32_t& t, diag::Profile profile = diag::Profile::Full) {
    rig.product.diag().arm(profile);
    rig.run(t, t + 100);
    t += 100;
    REQUIRE(rig.product.capture().capturing());
}

void stop(Rig& rig, uint32_t& t) {
    rig.product.diag().disarm();
    rig.run(t, t + 4000);
    t += 4000;
    REQUIRE_FALSE(rig.product.capture().capturing());
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

std::vector<diag::Record> armed_taxi(Rig& rig, uint32_t& t, uint32_t seconds) {
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, t, 3);
    arm(rig, t);
    taxi(rig, t, seconds);
    stop(rig, t);
    return captured(rig);
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

    const int duty = count_of(records, diag::Type::Duty);
    CHECK(duty >= 2);
    CHECK(duty <= 4);
    CHECK(count_of(records, diag::Type::Power) == duty);
    CHECK(paired_with_power(records) == duty);
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
