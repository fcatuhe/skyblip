// What the device decided: the build it booted, the dwell, the flight state, the alarm, the write.
#include <cstring>

#include "core/diag/payload.h"
#include "doctest/doctest.h"
#include "test/support/diag_round_trip.h"

using namespace skyblip;

TEST_CASE("diag record: boot carries the image and the parts the corpus was taken on") {
    diag::Boot in{};
    in.capabilities = 0x00000FAB;
    in.fw_build = 41237;
    in.fw_revision = 1290;
    in.fw_major = 2;
    in.fw_minor = 7;
    in.reset = power::ResetReason::Watchdog;
    in.image_state = dfu::ImageState::Probation;

    const diag::Boot out = diag_round_trip(in);
    CHECK(out.capabilities == in.capabilities);
    CHECK(out.fw_build == in.fw_build);
    CHECK(out.fw_revision == in.fw_revision);
    CHECK(out.fw_major == in.fw_major);
    CHECK(out.fw_minor == in.fw_minor);
    CHECK(out.reset == in.reset);
    CHECK(out.image_state == in.image_state);
}

TEST_CASE("diag record: config carries the settings a replay has to assume") {
    diag::Config in{};
    in.addr = 0x5BCAFE;
    in.battery_offset_mv = -137;
    in.freq_trim_e1_ppm = 62;
    in.aircraft_type = 9;
    in.addr_table = 7;
    in.alarm_volume = 4;
    in.settings_version = 1;
    in.alarm_enabled = true;
    in.metric = true;

    const diag::Config out = diag_round_trip(in);
    CHECK(out.addr == in.addr);
    CHECK(out.battery_offset_mv == in.battery_offset_mv);
    CHECK(out.freq_trim_e1_ppm == in.freq_trim_e1_ppm);
    CHECK(out.aircraft_type == in.aircraft_type);
    CHECK(out.addr_table == in.addr_table);
    CHECK(out.alarm_volume == in.alarm_volume);
    CHECK(out.settings_version == in.settings_version);
    CHECK(out.alarm_enabled);
    CHECK(out.metric);
}

TEST_CASE("diag record: a dwell carries the plan the second ran under and its refusal") {
    diag::Dwell in{};
    in.freq_hz = timing::kMband1Hz;
    in.start_ms = 800;
    in.end_ms = 1200;
    in.phase_ms = 947;
    in.duty_permille = 7;
    in.state = timing::SlotState::Slot1;
    in.band = model::Band::M;
    in.refusal = diag::Refusal::OverBudget;
    in.noise_dbm = -105;
    in.tx_allowed = true;
    in.own_tx_dwell = true;
    in.listen_only = true;
    in.armed = true;
    in.burst_armed = true;

    const diag::Dwell out = diag_round_trip(in);
    CHECK(out.freq_hz == in.freq_hz);
    CHECK(out.start_ms == in.start_ms);
    CHECK(out.end_ms == in.end_ms);
    CHECK(out.phase_ms == in.phase_ms);
    CHECK(out.duty_permille == in.duty_permille);
    CHECK(out.state == in.state);
    CHECK(out.band == in.band);
    CHECK(out.refusal == in.refusal);
    CHECK(out.noise_dbm == in.noise_dbm);
    CHECK(out.tx_allowed);
    CHECK(out.own_tx_dwell);
    CHECK(out.listen_only);
    CHECK(out.armed);
    CHECK(out.burst_armed);
}

TEST_CASE("diag record: a flight decision keeps the inputs that were one threshold away") {
    diag::Flight in{};
    in.speed_mm_s = 11940;
    in.climb_mm_s = -1250;
    in.alt_msl_m = 1487;
    in.hdop_e2 = 240;
    in.vdop_e2 = 310;
    in.declared = flight::FlightState::OnGround;
    in.confirmed = flight::FlightState::Airborne;
    in.fix_valid = true;
    in.rolling = true;
    in.climb_valid = true;
    in.tx_settled = true;

    const diag::Flight out = diag_round_trip(in);
    CHECK(out.speed_mm_s == in.speed_mm_s);
    CHECK(out.climb_mm_s == in.climb_mm_s);
    CHECK(out.alt_msl_m == in.alt_msl_m);
    CHECK(out.hdop_e2 == in.hdop_e2);
    CHECK(out.vdop_e2 == in.vdop_e2);
    CHECK(out.declared == in.declared);
    CHECK(out.confirmed == in.confirmed);
    CHECK(out.fix_valid);
    CHECK(out.rolling);
    CHECK(out.climb_valid);
    CHECK(out.tx_settled);
}

TEST_CASE("diag record: a target keeps the geometry the alarm level was decided on") {
    diag::Traffic in{};
    in.addr = 0x3F1002;
    in.dist_m = 2740;
    in.bearing_deg = 291;
    in.rel_alt_m = -180;
    in.closing_mps = 47;
    in.alarm = traffic::Level::Advisory;
    in.source = model::Source::AdslUplink;
    in.rssi_dbm = -101;
    in.tracked = 12;
    in.assessed = true;
    in.dismissed = true;
    in.in_formation = true;
    in.position_valid = true;

    const diag::Traffic out = diag_round_trip(in);
    CHECK(out.addr == in.addr);
    CHECK(out.dist_m == in.dist_m);
    CHECK(out.bearing_deg == in.bearing_deg);
    CHECK(out.rel_alt_m == in.rel_alt_m);
    CHECK(out.closing_mps == in.closing_mps);
    CHECK(out.alarm == in.alarm);
    CHECK(out.source == in.source);
    CHECK(out.rssi_dbm == in.rssi_dbm);
    CHECK(out.tracked == in.tracked);
    CHECK(out.assessed);
    CHECK(out.dismissed);
    CHECK(out.in_formation);
    CHECK(out.position_valid);
}

TEST_CASE("diag record: a durable write keeps where in the second it was placed") {
    diag::Write in{};
    in.waited_ms = 2870;
    in.phase_ms = 203;
    in.requests = 14;
    in.writes = 3;
    in.forced = 1;
    in.placement = timing::DurableWriteVerdict::Forced;
    in.kind = power::DurableWrite::FlightRecord;
    in.pending = true;

    const diag::Write out = diag_round_trip(in);
    CHECK(out.waited_ms == in.waited_ms);
    CHECK(out.phase_ms == in.phase_ms);
    CHECK(out.requests == in.requests);
    CHECK(out.writes == in.writes);
    CHECK(out.forced == in.forced);
    CHECK(out.placement == in.placement);
    CHECK(out.kind == in.kind);
    CHECK(out.pending);
}

TEST_CASE("diag record: the glass keeps what was on it and how long it had been") {
    diag::Screen in{};
    in.since_ms = 61000;
    in.page = 4;
    in.mode = 1;
    in.prompt = 2;
    in.alarm = traffic::Level::Advisory;
    in.backlight = true;
    in.powered = true;
    in.holding = true;

    const diag::Screen out = diag_round_trip(in);
    CHECK(out.since_ms == in.since_ms);
    CHECK(out.page == in.page);
    CHECK(out.mode == in.mode);
    CHECK(out.prompt == in.prompt);
    CHECK(out.alarm == in.alarm);
    CHECK(out.backlight);
    CHECK(out.powered);
    CHECK(out.holding);
}

TEST_CASE("diag record: a gap names its own size, so a hole is never silent") {
    diag::Gap in{};
    in.dropped = 37;
    in.span_ms = 1204;
    in.total = 91;
    in.capacity = 64;

    const diag::Gap out = diag_round_trip(in);
    CHECK(out.dropped == in.dropped);
    CHECK(out.span_ms == in.span_ms);
    CHECK(out.total == in.total);
    CHECK(out.capacity == in.capacity);
}

// The one record that says the tail is not torn: a session without it was cut.
TEST_CASE("diag record: the end marker counts the session it closes") {
    diag::End in{};
    in.records = 41200;
    in.dropped = 17;

    const diag::End out = diag_round_trip(in);
    CHECK(out.records == in.records);
    CHECK(out.dropped == in.dropped);
}

TEST_CASE("diag record: duty carries how long each consumer was on") {
    diag::Duty in{};
    in.panel_partial_refreshes = 1904;
    in.panel_full_refreshes = 37;
    in.backlight_ms = 41250;
    in.rx_armed_ms = 58300;
    in.tx_keyed_ms = 1420;
    in.ble_connected_ms = 22700;
    in.annunciator_ms = 640;

    const diag::Duty out = diag_round_trip(in);
    CHECK(out.panel_partial_refreshes == in.panel_partial_refreshes);
    CHECK(out.panel_full_refreshes == in.panel_full_refreshes);
    CHECK(out.backlight_ms == in.backlight_ms);
    CHECK(out.rx_armed_ms == in.rx_armed_ms);
    CHECK(out.tx_keyed_ms == in.tx_keyed_ms);
    CHECK(out.ble_connected_ms == in.ble_connected_ms);
    CHECK(out.annunciator_ms == in.annunciator_ms);
}

TEST_CASE("diag record: a duty counter past 16 bits wraps, because a clamp would lose the delta") {
    diag::Duty in{};
    in.backlight_ms = 65536 + 2000;
    in.rx_armed_ms = 4000000000u;
    in.panel_partial_refreshes = 70000;

    const diag::Duty out = diag_round_trip(in);
    CHECK(out.backlight_ms == 2000);
    CHECK(out.rx_armed_ms == 4000000000u % 65536u);
    CHECK(out.panel_partial_refreshes == 70000 - 65536);
}

namespace {

constexpr uint32_t kSixteenBits = 0xFFFF;

// The same 48 bytes scripts/test_blip.py decodes, so both ends of the wire read one pair.
constexpr uint8_t kDutyBeforeWrap[diag::kRecordBytes] = {
    0x12, 0x02, 0x00, 0x00, 0x40, 0xf9, 0xa1, 0x6a, 0x00, 0x00, 0x00, 0x00,
    0xe8, 0xfd, 0x60, 0xea, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kDutyAfterWrap[diag::kRecordBytes] = {
    0x12, 0x02, 0x00, 0x00, 0x5e, 0xf9, 0xa1, 0x6a, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x73, 0x90, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

diag::Duty duty_off_the_wire(const diag::Duty& value, uint32_t at_s, uint8_t* raw) {
    diag::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    diag::encode_record(diag::record_of(value, at), raw);
    diag::Record record{};
    REQUIRE(diag::decode_record(raw, record) == Status::Ok);
    diag::Duty out{};
    REQUIRE(diag::read(record, out));
    return out;
}

uint32_t duty_delta(uint32_t later, uint32_t earlier) { return (later - earlier) & kSixteenBits; }

}  // namespace

TEST_CASE("diag record: two duty records a wrap apart subtract to the true interval") {
    diag::Duty before{};
    before.backlight_ms = 65000;
    before.rx_armed_ms = 60000;
    diag::Duty after{};
    after.backlight_ms = 95000;
    after.rx_armed_ms = 90000;

    uint8_t first_raw[diag::kRecordBytes]{};
    uint8_t second_raw[diag::kRecordBytes]{};
    const diag::Duty first = duty_off_the_wire(before, kDiagTestUtc, first_raw);
    const diag::Duty second = duty_off_the_wire(after, kDiagTestUtc + 30, second_raw);
    CHECK(std::memcmp(first_raw, kDutyBeforeWrap, diag::kRecordBytes) == 0);
    CHECK(std::memcmp(second_raw, kDutyAfterWrap, diag::kRecordBytes) == 0);

    // 95,000 and 90,000 ms leave 29,464 and 24,464 once the top half is dropped
    CHECK(second.backlight_ms == 29464);
    CHECK(second.rx_armed_ms == 24464);
    CHECK(duty_delta(second.backlight_ms, first.backlight_ms) == 30000);
    CHECK(duty_delta(second.rx_armed_ms, first.rx_armed_ms) == 30000);
}
