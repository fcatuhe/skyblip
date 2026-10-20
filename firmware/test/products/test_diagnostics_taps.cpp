// One case per record type, at the service that decides the fact it carries.
#include <cstring>
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

bool link_action(const std::vector<diagnostics::Record>& records, diagnostics::LinkAction action,
                 diagnostics::Link& out) {
    for (const diagnostics::Record& record : records) {
        diagnostics::Link found{};
        if (!diagnostics::read(record, found) || found.action != action) continue;
        out = found;
        return true;
    }
    return false;
}

// The receiver model doing the talking, rather than the rig pushing solutions past it.
void receiver_seconds(Rig& rig, uint32_t& now_ms, uint32_t seconds) {
    for (uint32_t elapsed = 0; elapsed < seconds * 1000; elapsed += 50) {
        rig.platform.chips().gnss.tick(now_ms);
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        now_ms += 50;
    }
}

}  // namespace

TEST_CASE("diagnostics boot: arming names the build and the parts that produced the corpus") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 3);

    CHECK(count_of(records, diagnostics::Type::Boot) == 1);
    diagnostics::Boot boot{};
    REQUIRE(first_of(records, boot));
    CHECK(boot.capabilities == static_cast<uint32_t>(rig.product.capabilities()));
    CHECK(boot.reset == rig.product.reset_reason());
    CHECK(boot.image_state == dfu::ImageState::Confirmed);
}

TEST_CASE("diagnostics config: arming names the settings every other record was decided under") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    rig.settings().aircraft_type = 9;
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.settings().battery_offset_mv = -30;
    rig.settings().battery_offset_manual = true;
    rig.settings().units = go::Units::Metric;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    taxi(rig, now_ms, 2);
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    CHECK(count_of(records, diagnostics::Type::Config) == 1);
    diagnostics::Config config{};
    REQUIRE(first_of(records, config));
    CHECK(config.address == settings::address_of(platform::host::Identity::kSerial));
    CHECK(config.address_mapping_table == settings::kAddressMappingTable);
    CHECK(config.aircraft_type == 9);
    CHECK(config.alarm_volume == 1);
    CHECK(config.battery_offset_mv == -30);
    CHECK(config.battery_trim_manual);
    CHECK(config.metric);
    CHECK(config.alarm_enabled);
    CHECK(config.settings_version == go::Settings::kCurrentVersion);
}

TEST_CASE(
    "diagnostics config: arming names the power the radio transmits at, as its executor set it") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 2);

    diagnostics::Config config{};
    REQUIRE(first_of(records, config));
    CHECK(config.tx_power_dbm == parts::sx1262::kConductedDbm);
    CHECK(config.pa_rated_dbm == parts::sx1262::kPaConfigHighPowerRatedDbm);
}

TEST_CASE("diagnostics gnss: every solution is recorded with what the sky gave it") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 5);

    CHECK(count_of(records, diagnostics::Type::Gnss) >= 4);
    diagnostics::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.fix_valid);
    CHECK(gnss.satellites == 10);
    CHECK(gnss.hdop_e2 == 100);
    CHECK(gnss.stage == gnss::Stage::Fixed);
    CHECK(gnss.pps_locked);
}

TEST_CASE("diagnostics gnss: a solution the receiver refused carries the reason it was refused") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.platform.chips().gnss.fix_valid = false;
    receiver_seconds(rig, now_ms, 5);
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    diagnostics::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.reject == gnss::FixReject::NoSolution);
    CHECK_FALSE(gnss.fix_valid);
}

TEST_CASE("diagnostics gnss: a solution the receiver accepted carries no reject at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    receiver_seconds(rig, now_ms, 5);
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    diagnostics::Gnss gnss{};
    REQUIRE(last_of(records, gnss));
    CHECK(gnss.reject == gnss::FixReject::None);
    CHECK(gnss.fix_valid);
}

TEST_CASE("diagnostics pps: one record an edge, and the interval it measured against a second") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 6);

    CHECK(count_of(records, diagnostics::Type::Pps) >= 5);
    CHECK(count_of(records, diagnostics::Type::Pps) <= 8);
    diagnostics::Pps pps{};
    REQUIRE(last_of(records, pps));
    CHECK(pps.locked);
    CHECK(pps.interval_us == 1000000);
    CHECK(pps.error_us == 0);
    CHECK(pps.since_edge_ms < 1000);
}

TEST_CASE("diagnostics pps: a receiver that lost its edge still writes a record a second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.platform.pps().set_locked(false);
    taxi(rig, now_ms, 5);
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    CHECK(count_of(records, diagnostics::Type::Pps) >= 4);
    diagnostics::Pps pps{};
    REQUIRE(last_of(records, pps));
    CHECK_FALSE(pps.locked);
    CHECK(pps.interval_us == 0);
}

TEST_CASE("diagnostics dwell: every dwell the radio arms is a record, band edges and hop alike") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 4);

    CHECK(count_of(records, diagnostics::Type::Dwell) >= 3 * 4);
    bool uplink = false;
    bool slot0 = false;
    bool slot1 = false;
    for (const diagnostics::Record& record : records) {
        diagnostics::Dwell dwell{};
        if (!diagnostics::read(record, dwell)) continue;
        CHECK(dwell.armed);
        uplink = uplink || dwell.frequency_hz == timing::kOBandHz;
        slot0 = slot0 || dwell.frequency_hz == timing::kMBand0Hz;
        slot1 = slot1 || dwell.frequency_hz == timing::kMBand1Hz;
        if (dwell.state == timing::SlotState::Slot1) CHECK(dwell.end_ms == timing::kSlot1EndMs);
    }
    CHECK(uplink);
    CHECK(slot0);
    CHECK(slot1);
}

// The executor's worst-since-boot figures could not date the one 5.9 ms hop the bench saw.
TEST_CASE("diagnostics switch: every dwell change is recorded, with the channel it tuned to") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 4);

    CHECK(count_of(records, diagnostics::Type::Switch) >= 3 * 3);
    bool hop = false;
    bool to_o_band = false;
    bool to_m_band = false;
    for (const diagnostics::Record& record : records) {
        diagnostics::Switch switch_record{};
        if (!diagnostics::read(record, switch_record)) continue;
        CHECK(switch_record.armed_ahead);
        if (switch_record.change == diagnostics::SwitchKind::Hop)
            hop = switch_record.to_hz == timing::kMBand1Hz;
        if (switch_record.change == diagnostics::SwitchKind::ToOBand)
            to_o_band = switch_record.to_hz == timing::kOBandHz;
        if (switch_record.change == diagnostics::SwitchKind::ToMBand)
            to_m_band = switch_record.to_hz == timing::kMBand0Hz;
    }
    CHECK(hop);
    CHECK(to_o_band);
    CHECK(to_m_band);
}

TEST_CASE("diagnostics barometer: a sample carries the altitude and the rate taken from it") {
    Rig rig;
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 6);

    CHECK(count_of(records, diagnostics::Type::Barometer) >= 4);
    diagnostics::Barometer barometer{};
    REQUIRE(last_of(records, barometer));
    CHECK(barometer.pressure_mpa > 0);
    CHECK(barometer.active);
    CHECK(barometer.altitude_mm != 0);
}

TEST_CASE("diagnostics motion: the hub's axes are recorded on their own cadence") {
    Rig rig;
    uint32_t now_ms = 100;
    const int seconds = 6;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, seconds);

    CHECK(count_of(records, diagnostics::Type::Motion) >= seconds - 1);
    CHECK(count_of(records, diagnostics::Type::Motion) <= seconds + 1);
    diagnostics::Motion motion{};
    REQUIRE(last_of(records, motion));
    CHECK(motion.fitted);
    CHECK(motion.imu_error == rig.state().imu.error);
}

TEST_CASE("diagnostics motion: a unit with no hub writes no motion record at all") {
    constexpr ports::Capabilities kNoHub = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Inclinometer));
    Rig rig{kNoHub};
    uint32_t now_ms = 100;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, 4);

    CHECK(count_of(records, diagnostics::Type::Motion) == 0);
    CHECK(count_of(records, diagnostics::Type::Power) > 0);
}

TEST_CASE(
    "diagnostics contact: both edges of a press are recorded with what the product made of them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    CHECK(count_of(records, diagnostics::Type::Contact) == 2);
    diagnostics::Contact down{};
    REQUIRE(first_of(records, down));
    CHECK(down.contact == events::Contact::Button);
    CHECK(down.down);
    CHECK(down.gesture == static_cast<uint8_t>(go::Gesture::None));
    diagnostics::Contact up{};
    REQUIRE(last_of(records, up));
    CHECK_FALSE(up.down);
    CHECK(up.gesture == static_cast<uint8_t>(go::Gesture::Press));
    CHECK(up.held_ms == up.at_ms - down.at_ms);
}

TEST_CASE("diagnostics link: a central connecting is a record, with the payload it negotiated") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.raise_link(7);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    rig.send("{\"command\":\"status\"}");
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    diagnostics::Link up{};
    REQUIRE(link_action(records, diagnostics::LinkAction::Up, up));
    CHECK(up.link_session == 7);
    CHECK(up.payload_bytes == rig.platform.link().payload_bytes());
    diagnostics::Link received{};
    REQUIRE(link_action(records, diagnostics::LinkAction::Received, received));
    CHECK(received.endpoint == events::Endpoint::Config);
    CHECK(received.link_session == 7);
    CHECK(received.frame_bytes > 0);
    diagnostics::Link claimed{};
    REQUIRE(link_action(records, diagnostics::LinkAction::ClaimTaken, claimed));
    CHECK(claimed.claim_held);
    CHECK(claimed.holder == 7);
}

// One record a connection, the Down it already wrote, answers whether option B is needed.
TEST_CASE(
    "diagnostics link: a link that ended says who exchanged its MTU, when, and what it cost") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayloadBytes);
    rig.raise_link(4);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    std::memcpy(rig.settings().callsign, "ABCDEFGHI", 10);
    rig.send("{\"command\":\"get\"}");
    rig.run(now_ms, now_ms + 300);
    now_ms += 300;
    rig.platform.link().exchange_mtu_by_unit(4, 244);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    rig.drop_link(4);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    diagnostics::Link down{};
    REQUIRE(link_action(records, diagnostics::LinkAction::Down, down));
    CHECK(down.link_session == 4);
    CHECK(down.payload_bytes == 244);
    CHECK(down.mtu_exchanged_by_unit);
    CHECK_FALSE(down.mtu_exchanged_by_central);
    CHECK(down.mtu_request == events::MtuRequest::Exchanged);
    CHECK(down.replies_too_wide == 1);
    CHECK(down.mtu_settled_after_ms >= 500);
    CHECK(down.mtu_settled_after_ms <= 800);
}

TEST_CASE(
    "diagnostics traffic: an aircraft is recorded a reception, with the margin it was graded on") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);

    for (int i = 0; i < 4; i++) {
        model::AircraftObservation observation{};
        observation.address = 0x515151;
        observation.address_mapping_table = 6;
        observation.position_valid = true;
        observation.speed_valid = true;
        observation.speed_q = 40 * 4;
        observation.altitude_m = 300;
        observation.latitude_1e7 = rig.state().own.latitude_1e7 + 9000;
        observation.longitude_1e7 = rig.state().own.longitude_1e7;
        observation.at_ms = now_ms;
        observation.rssi_dbm = -92;
        observation.source = model::Source::AdslDirect;
        observation.received.at_s = rig.state().traffic_now_s(now_ms);
        rig.state().traffic.update(observation, rig.state().traffic_now_s(now_ms));
        taxi(rig, now_ms, 1);
    }
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    CHECK(count_of(records, diagnostics::Type::Traffic) >= 3);
    diagnostics::Traffic seen{};
    REQUIRE(last_of(records, seen));
    CHECK(seen.address == 0x515151);
    CHECK(seen.source == model::Source::AdslDirect);
    CHECK(seen.rssi_dbm == -92);
    CHECK(seen.tracked == 1);
    CHECK(seen.assessed);
    CHECK(seen.distance_m > 0);
    CHECK(seen.distance_m < 2000);
}

TEST_CASE(
    "diagnostics write: a pending settings write is recorded every evaluation, not only the "
    "write") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm(rig, now_ms);
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.product.config_link().config().note_settings_changed();
    taxi(rig, now_ms, 6);
    stop(rig, now_ms);
    const std::vector<diagnostics::Record> records = captured(rig);

    CHECK(count_of(records, diagnostics::Type::Write) > 1);
    bool placed = false;
    for (const diagnostics::Record& record : records) {
        diagnostics::Write write{};
        if (!diagnostics::read(record, write)) continue;
        CHECK(write.kind == power::DurableWrite::Settings);
        CHECK(write.placement != timing::DurableWriteVerdict::Idle);
        placed = placed || write.placement == timing::DurableWriteVerdict::Place;
        if (write.placement == timing::DurableWriteVerdict::Place)
            CHECK(write.waited_ms >= timing::DurableWriteWindow::kSettleMs);
    }
    CHECK(placed);
}

TEST_CASE("diagnostics screen: what was on the glass is recorded on the render cadence") {
    Rig rig;
    uint32_t now_ms = 100;
    const int seconds = 8;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, seconds);

    CHECK(count_of(records, diagnostics::Type::Screen) >= seconds - 1);
    CHECK(count_of(records, diagnostics::Type::Screen) <= seconds + 1);
    diagnostics::Screen screen{};
    REQUIRE(last_of(records, screen));
    CHECK(screen.page == static_cast<uint8_t>(rig.product.screen().page()));
    CHECK(screen.mode == static_cast<uint8_t>(go::Mode::Page));
    CHECK(screen.powered);
    CHECK_FALSE(screen.holding);
    CHECK(screen.since_page_shown_ms > 0);
}

// The fix processed later every second was the loop's lateness, so the capture has to carry it.
TEST_CASE(
    "diagnostics loop: a capture carries one loop window a second, with the passes that ran") {
    Rig rig;
    uint32_t now_ms = 100;
    const int seconds = 8;
    const std::vector<diagnostics::Record> records = armed_taxi(rig, now_ms, seconds);

    CHECK(count_of(records, diagnostics::Type::Loop) >= seconds - 1);
    CHECK(count_of(records, diagnostics::Type::Loop) <= seconds + 1);
    diagnostics::Loop window{};
    REQUIRE(last_of(records, window));
    CHECK(window.passes > 0);
    CHECK(window.worst_service <= diagnostics::kLoopBoardPoll);
}

// The page turns this into the hours a partition lasts, so it is a claim about the taps.
TEST_CASE("diagnostics: the rate the capture page quotes covers a quiet airborne second") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 8);
    arm(rig, now_ms);
    const uint32_t before = rig.product.capture().records_written();
    const uint32_t seconds = 20;
    rig.seconds(now_ms, seconds, 50000, 800);
    const uint32_t measured = (rig.product.capture().records_written() - before) / seconds;
    MESSAGE("airborne, empty sky: " << measured << " records a second");
    CHECK(measured <= diagnostics::Recorder::kPeriodicRecordsPerSecond);
    CHECK(measured + 2 >= diagnostics::Recorder::kPeriodicRecordsPerSecond);
}

// The one fact every tap shares: disarmed, the arguments may be gathered but nothing is kept.
TEST_CASE("diagnostics: a device nobody armed records nothing at all") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    rig.raise_link(3);
    rig.press(now_ms);
    rig.settings().alarm_volume = go::AlarmVolume::Soft;
    rig.product.config_link().config().note_settings_changed();
    taxi(rig, now_ms, 6);

    CHECK(rig.product.diagnostics().written() == 0);
    CHECK(rig.product.diagnostics().queued() == 0);
    CHECK(rig.product.capture().records_written() == 0);
    CHECK(captured(rig).empty());
}
