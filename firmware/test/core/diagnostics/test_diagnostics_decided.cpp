// What the device decided: the build it booted, the dwell, the flight state, the alarm, the write.
#include <cstring>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_round_trip.h"

using namespace skyblip;

TEST_CASE("diag record: boot carries the image and the parts the corpus was taken on") {
    diagnostics::Boot in{};
    in.capabilities = 0x00000FAB;
    in.fw_build = 41237;
    in.fw_revision = 1290;
    in.fw_major = 2;
    in.fw_minor = 7;
    in.reset = power::ResetReason::Watchdog;
    in.image_state = dfu::ImageState::Probation;
    in.profile = diagnostics::Profile::FlightRun;
    in.profile_recorded = true;

    const diagnostics::Boot out = diagnostics_round_trip(in);
    CHECK(out.capabilities == in.capabilities);
    CHECK(out.fw_build == in.fw_build);
    CHECK(out.fw_revision == in.fw_revision);
    CHECK(out.fw_major == in.fw_major);
    CHECK(out.fw_minor == in.fw_minor);
    CHECK(out.reset == in.reset);
    CHECK(out.image_state == in.image_state);
    CHECK(out.profile == diagnostics::Profile::FlightRun);
    CHECK(out.profile_recorded);
}

// A capture from before the byte has a zero there, and zero is Full: the flag says it was never
// set.
TEST_CASE("diag record: a boot older than its profile byte reads as unrecorded, not as full") {
    diagnostics::Boot in{};
    in.profile = diagnostics::Profile::PowerRun;
    in.profile_recorded = true;
    const diagnostics::Record written = diagnostics::record_of(in, diagnostics::Instant{});
    CHECK(written.payload[14] == 1);
    CHECK(written.flagged(diagnostics::kBootFlagProfileRecorded));

    const diagnostics::Record older =
        diagnostics::record_of(diagnostics::Boot{}, diagnostics::Instant{});
    CHECK(older.payload[14] == 0);
    diagnostics::Boot out{};
    REQUIRE(diagnostics::read(older, out));
    CHECK_FALSE(out.profile_recorded);
}

TEST_CASE("diag record: config carries the settings a replay has to assume") {
    diagnostics::Config in{};
    in.address = 0x5BCAFE;
    in.battery_offset_mv = -137;
    in.frequency_trim_e1_ppm = 62;
    in.aircraft_type = 9;
    in.address_mapping_table = 7;
    in.alarm_volume = 4;
    in.settings_version = 1;
    in.alarm_enabled = true;
    in.metric = true;

    const diagnostics::Config out = diagnostics_round_trip(in);
    CHECK(out.address == in.address);
    CHECK(out.battery_offset_mv == in.battery_offset_mv);
    CHECK(out.frequency_trim_e1_ppm == in.frequency_trim_e1_ppm);
    CHECK(out.aircraft_type == in.aircraft_type);
    CHECK(out.address_mapping_table == in.address_mapping_table);
    CHECK(out.alarm_volume == in.alarm_volume);
    CHECK(out.settings_version == in.settings_version);
    CHECK(out.alarm_enabled);
    CHECK(out.metric);
}

TEST_CASE("diag record: config carries the power the transmitter was asked for and its PA row") {
    diagnostics::Config in{};
    in.tx_power_dbm = -9;
    in.pa_rated_dbm = 22;

    const diagnostics::Config out = diagnostics_round_trip(in);
    CHECK(out.tx_power_dbm == -9);
    CHECK(out.pa_rated_dbm == 22);
}

namespace {

// The same 24 bytes scripts/test_blip.py decodes, so both ends of the wire read one transmitter.
constexpr uint8_t kConfigOfATransmitter[diagnostics::kRecordBytes] = {
    0x02, 0x02, 0x00, 0x00, 0x40, 0xf9, 0xa1, 0x6a, 0xfe, 0xca, 0x5b, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x0e, 0x16, 0x00, 0x00};

}  // namespace

TEST_CASE("diag record: the transmitter's power sits in config bytes 12 and 13 on the wire") {
    diagnostics::Config in{};
    in.address = 0x5BCAFE;
    in.address_mapping_table = 7;
    in.settings_version = 1;
    in.tx_power_dbm = 14;
    in.pa_rated_dbm = 22;
    diagnostics::Instant at{};
    at.at_s = kDiagnosticsTestUtcS;
    at.utc_dated = true;

    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(diagnostics::record_of(in, at), raw);
    CHECK(std::memcmp(raw, kConfigOfATransmitter, diagnostics::kRecordBytes) == 0);
}

TEST_CASE("diag record: a dwell carries the plan the second ran under and its refusal") {
    diagnostics::Dwell in{};
    in.frequency_hz = timing::kMband1Hz;
    in.start_ms = 800;
    in.end_ms = 1200;
    in.phase_ms = 947;
    in.duty_permille = 7;
    in.state = timing::SlotState::Slot1;
    in.band = model::Band::M;
    in.refusal = diagnostics::Refusal::OverBudget;
    in.noise_dbm = -105;
    in.tx_allowed = true;
    in.own_tx_dwell = true;
    in.listen_only = true;
    in.armed = true;
    in.burst_armed = true;

    const diagnostics::Dwell out = diagnostics_round_trip(in);
    CHECK(out.frequency_hz == in.frequency_hz);
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
    diagnostics::Flight in{};
    in.speed_mm_s = 11940;
    in.climb_mm_s = -1250;
    in.altitude_msl_m = 1487;
    in.hdop_e2 = 240;
    in.vdop_e2 = 310;
    in.declared = flight::FlightState::Ground;
    in.confirmed = flight::FlightState::Airborne;
    in.fix_valid = true;
    in.rolling = true;
    in.climb_valid = true;
    in.tx_settled = true;

    const diagnostics::Flight out = diagnostics_round_trip(in);
    CHECK(out.speed_mm_s == in.speed_mm_s);
    CHECK(out.climb_mm_s == in.climb_mm_s);
    CHECK(out.altitude_msl_m == in.altitude_msl_m);
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
    diagnostics::Traffic in{};
    in.address = 0x3F1002;
    in.distance_m = 2740;
    in.bearing_deg = 291;
    in.relative_altitude_m = -180;
    in.closing_mps = 47;
    in.alarm = traffic::Level::Advisory;
    in.source = model::Source::AdslUplink;
    in.rssi_dbm = -101;
    in.tracked = 12;
    in.assessed = true;
    in.dismissed = true;
    in.in_formation = true;
    in.position_valid = true;

    const diagnostics::Traffic out = diagnostics_round_trip(in);
    CHECK(out.address == in.address);
    CHECK(out.distance_m == in.distance_m);
    CHECK(out.bearing_deg == in.bearing_deg);
    CHECK(out.relative_altitude_m == in.relative_altitude_m);
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
    diagnostics::Write in{};
    in.waited_ms = 2870;
    in.phase_ms = 203;
    in.requests = 14;
    in.writes = 3;
    in.forced = 1;
    in.placement = timing::DurableWriteVerdict::Forced;
    in.kind = power::DurableWrite::FlightRecord;
    in.pending = true;

    const diagnostics::Write out = diagnostics_round_trip(in);
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
    diagnostics::Screen in{};
    in.since_ms = 61000;
    in.page = 4;
    in.mode = 1;
    in.prompt = 2;
    in.alarm = traffic::Level::Advisory;
    in.backlight = true;
    in.powered = true;
    in.holding = true;
    in.render_us = 187430;
    in.present_us = 41210;

    const diagnostics::Screen out = diagnostics_round_trip(in);
    CHECK(out.since_ms == in.since_ms);
    CHECK(out.page == in.page);
    CHECK(out.mode == in.mode);
    CHECK(out.prompt == in.prompt);
    CHECK(out.alarm == in.alarm);
    CHECK(out.backlight);
    CHECK(out.powered);
    CHECK(out.holding);
    CHECK(out.render_us == in.render_us);
    CHECK(out.present_us == in.present_us);
}

TEST_CASE("diag record: a loop window keeps its longest pass and its longest wait, each dated") {
    diagnostics::Loop in{};
    in.passes = 87;
    in.busy_ms = 231;
    in.worst_pass_us = 191204;
    in.worst_pass_phase_ms = 203;
    in.worst_gap_ms = 201;
    in.worst_gap_phase_ms = 394;
    in.worst_service = 9;
    in.worst_tick_ms = 190;

    const diagnostics::Loop out = diagnostics_round_trip(in);
    CHECK(out.passes == in.passes);
    CHECK(out.busy_ms == in.busy_ms);
    CHECK(out.worst_pass_us == in.worst_pass_us);
    CHECK(out.worst_pass_phase_ms == in.worst_pass_phase_ms);
    CHECK(out.worst_gap_ms == in.worst_gap_ms);
    CHECK(out.worst_gap_phase_ms == in.worst_gap_phase_ms);
    CHECK(out.worst_service == in.worst_service);
    CHECK(out.worst_tick_ms == in.worst_tick_ms);
}

TEST_CASE("diag record: a gap names its own size, so a hole is never silent") {
    diagnostics::Gap in{};
    in.dropped = 37;
    in.span_ms = 1204;
    in.total = 91;
    in.capacity = 64;

    const diagnostics::Gap out = diagnostics_round_trip(in);
    CHECK(out.dropped == in.dropped);
    CHECK(out.span_ms == in.span_ms);
    CHECK(out.total == in.total);
    CHECK(out.capacity == in.capacity);
}

// The one record that says the tail is not torn: a session without it was cut.
TEST_CASE("diag record: the end marker counts the session it closes") {
    diagnostics::End in{};
    in.records = 41200;
    in.dropped = 17;

    const diagnostics::End out = diagnostics_round_trip(in);
    CHECK(out.records == in.records);
    CHECK(out.dropped == in.dropped);
}

TEST_CASE("diag record: duty carries how long each consumer was on") {
    diagnostics::Duty in{};
    in.panel_partial_refreshes = 1904;
    in.panel_full_refreshes = 37;
    in.backlight_ms = 41250;
    in.rx_armed_ms = 58300;
    in.tx_keyed_ms = 1420;
    in.ble_connected_ms = 22700;
    in.annunciator_ms = 640;

    const diagnostics::Duty out = diagnostics_round_trip(in);
    CHECK(out.panel_partial_refreshes == in.panel_partial_refreshes);
    CHECK(out.panel_full_refreshes == in.panel_full_refreshes);
    CHECK(out.backlight_ms == in.backlight_ms);
    CHECK(out.rx_armed_ms == in.rx_armed_ms);
    CHECK(out.tx_keyed_ms == in.tx_keyed_ms);
    CHECK(out.ble_connected_ms == in.ble_connected_ms);
    CHECK(out.annunciator_ms == in.annunciator_ms);
}

TEST_CASE("diag record: a duty counter past 16 bits wraps, because a clamp would lose the delta") {
    diagnostics::Duty in{};
    in.backlight_ms = 65536 + 2000;
    in.rx_armed_ms = 4000000000u;
    in.panel_partial_refreshes = 70000;

    const diagnostics::Duty out = diagnostics_round_trip(in);
    CHECK(out.backlight_ms == 2000);
    CHECK(out.rx_armed_ms == 4000000000u % 65536u);
    CHECK(out.panel_partial_refreshes == 70000 - 65536);
}

namespace {

constexpr uint32_t kSixteenBits = 0xFFFF;

// The same 48 bytes scripts/test_blip.py decodes, so both ends of the wire read one pair.
constexpr uint8_t kDutyBeforeWrap[diagnostics::kRecordBytes] = {
    0x12, 0x02, 0x00, 0x00, 0x40, 0xf9, 0xa1, 0x6a, 0x00, 0x00, 0x00, 0x00,
    0xe8, 0xfd, 0x60, 0xea, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kDutyAfterWrap[diagnostics::kRecordBytes] = {
    0x12, 0x02, 0x00, 0x00, 0x5e, 0xf9, 0xa1, 0x6a, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x73, 0x90, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

diagnostics::Duty duty_off_the_wire(const diagnostics::Duty& value, uint32_t at_s, uint8_t* raw) {
    diagnostics::Instant at{};
    at.at_s = at_s;
    at.utc_dated = true;
    diagnostics::encode_record(diagnostics::record_of(value, at), raw);
    diagnostics::Record record{};
    REQUIRE(diagnostics::decode_record(raw, record) == Status::Ok);
    diagnostics::Duty out{};
    REQUIRE(diagnostics::read(record, out));
    return out;
}

uint32_t duty_delta(uint32_t later, uint32_t earlier) { return (later - earlier) & kSixteenBits; }

}  // namespace

TEST_CASE("diag record: two duty records a wrap apart subtract to the true interval") {
    diagnostics::Duty before{};
    before.backlight_ms = 65000;
    before.rx_armed_ms = 60000;
    diagnostics::Duty after{};
    after.backlight_ms = 95000;
    after.rx_armed_ms = 90000;

    uint8_t first_raw[diagnostics::kRecordBytes]{};
    uint8_t second_raw[diagnostics::kRecordBytes]{};
    const diagnostics::Duty first = duty_off_the_wire(before, kDiagnosticsTestUtcS, first_raw);
    const diagnostics::Duty second =
        duty_off_the_wire(after, kDiagnosticsTestUtcS + 30, second_raw);
    CHECK(std::memcmp(first_raw, kDutyBeforeWrap, diagnostics::kRecordBytes) == 0);
    CHECK(std::memcmp(second_raw, kDutyAfterWrap, diagnostics::kRecordBytes) == 0);

    // 95,000 and 90,000 ms leave 29,464 and 24,464 once the top half is dropped
    CHECK(second.backlight_ms == 29464);
    CHECK(second.rx_armed_ms == 24464);
    CHECK(duty_delta(second.backlight_ms, first.backlight_ms) == 30000);
    CHECK(duty_delta(second.rx_armed_ms, first.rx_armed_ms) == 30000);
}
