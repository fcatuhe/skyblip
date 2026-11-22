// The bytes each diagnostics record is written as, pinned to the vectors the host decoder reads.
#include <cstdint>
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/diagnostics/record.h"
#include "doctest/doctest.h"
#include "test/support/hex_vectors.h"

using namespace skyblip;
using namespace skyblip::diagnostics;

namespace {

constexpr const char* kPath = "../schemas/diagnostics_log.v1.vectors.json";

Instant vector_instant() {
    Instant at{};
    at.at_s = 1700000000;
    at.into_ms = 1200;
    at.phase_valid = true;
    at.utc_dated = true;
    return at;
}

std::vector<uint8_t> bytes_of(const Record& record) {
    std::vector<uint8_t> raw(kRecordBytes);
    encode_record(record, raw.data());
    return raw;
}

Boot boot() {
    Boot value{};
    value.capabilities = 0x1FFF;
    value.firmware_build = 4242;
    value.firmware_revision = 7;
    value.firmware_major = 1;
    value.firmware_minor = 2;
    value.reset = power::ResetReason::Brownout;
    value.image_state = dfu::ImageState::Probation;
    value.profile = Profile::FlightRun;
    value.profile_recorded = true;
    return value;
}

Config config() {
    Config value{};
    value.address = 0xABCDEF;
    value.battery_offset_mv = -120;
    value.frequency_trim_e1_ppm = -35;
    value.aircraft_type = 9;
    value.address_mapping_table = 7;
    value.settings_version = 2;
    value.tx_power_dbm = -9;
    value.pa_rated_dbm = 22;
    value.alarm_setting = 1;
    value.alarm_setting_recorded = true;
    value.metric = true;
    value.battery_trim_manual = true;
    return value;
}

Gnss gnss() {
    Gnss value{};
    value.solution_phase_ms = 350;
    value.residual_m = 12;
    value.hdop_e2 = 120;
    value.vdop_e2 = 180;
    value.stage_s = 900;
    value.satellites = 11;
    value.satellites_in_view = 17;
    value.fix_mode = 3;
    value.reject = gnss::FixReject::MissingGga;
    value.stage = gnss::Stage::Solving;
    value.fix_valid = true;
    value.residual_valid = true;
    value.pps_locked = true;
    value.geoid_measured = true;
    value.tx_settled = true;
    value.levels_fresh = true;
    return value;
}

Pps pps() {
    Pps value{};
    value.interval_us = 999987;
    value.error_us = -13;
    value.samples = 3600;
    value.holdover_events = 2;
    value.since_edge_ms = 450;
    value.locked = true;
    value.utc_valid = true;
    return value;
}

radio::Entry burst() {
    radio::Entry value{};
    value.verdict = radio::Verdict::Held;
    value.band = model::Band::O;
    value.source = model::Source::Alptas;
    value.address = 0x0102AB;
    value.at_s = 1700000000;
    value.into_ms = 1200;
    value.tx_keyed_us = 410;
    value.tx_span_us = 960;
    value.tx_stage_margin_us = -40;
    value.rssi_dbm = -97;
    value.key_offset_s = -2;
    value.channel = 1;
    value.bytes = 26;
    value.address_valid = true;
    value.rssi_valid = true;
    value.utc_dated = true;
    value.airborne = true;
    value.phase_valid = true;
    value.tx_span_valid = true;
    return value;
}

Dwell dwell() {
    Dwell value{};
    value.frequency_hz = 868200000;
    value.start_ms = 450;
    value.end_ms = 822;
    value.phase_ms = 500;
    value.duty_permille = 7;
    value.state = timing::SlotState::Slot0;
    value.band = model::Band::M;
    value.refusal = Refusal::GateClosed;
    value.noise_dbm = -110;
    value.tx_allowed = true;
    value.own_tx_dwell = true;
    value.listen_only = true;
    value.armed = true;
    value.burst_armed = true;
    return value;
}

Flight flight() {
    Flight value{};
    value.speed_mm_s = 12500;
    value.climb_mm_s = -1800;
    value.altitude_msl_m = 1234;
    value.hdop_e2 = 95;
    value.vdop_e2 = 140;
    value.declared = flight::FlightState::Airborne;
    value.confirmed = flight::FlightState::Ground;
    value.fix_valid = true;
    value.rolling = true;
    value.climb_valid = true;
    value.tx_settled = true;
    return value;
}

Power power() {
    Power value{};
    value.cell_mv = 3912;
    value.supply_warnings = 3;
    value.implausible = 4;
    value.charge_warnings = 5;
    value.die_dc = -45;
    value.sample_offset_mv = -6;
    value.percent = 71;
    value.power_level = power::PowerLevel::Low;
    value.charge = power::ChargeCondition::TooCold;
    value.charging = true;
    value.external_power = true;
    value.battery_valid = true;
    value.die_valid = true;
    return value;
}

Barometer barometer() {
    Barometer value{};
    value.pressure_mpa = 89874500;
    value.altitude_mm = 1012345;
    value.climb_mm_s = -2100;
    value.temperature_dc = 183;
    value.active = true;
    value.temperature_valid = true;
    value.climb_adopted = true;
    return value;
}

Motion motion() {
    Motion value{};
    value.slip_mg = -21;
    value.normal_mg = 1350;
    value.lateral_mg = -42;
    value.longitudinal_mg = 63;
    value.most_normal_mg = 2100;
    value.least_normal_mg = -300;
    value.imu_error = 1;
    value.sensor_error = 2;
    value.slip_valid = true;
    value.gload_valid = true;
    value.fitted = true;
    return value;
}

Contact contact() {
    Contact value{};
    value.at_ms = 123456;
    value.held_ms = 1100;
    value.contact = events::Contact::Pad;
    value.gesture = 2;
    value.down = true;
    return value;
}

Link link() {
    Link value{};
    value.link_session = 3;
    value.payload_bytes = 244;
    value.frame_bytes = 180;
    value.holder = 2;
    value.drops = 9;
    value.action = LinkAction::Dropped;
    value.endpoint = events::Endpoint::Log;
    value.mtu_exchange_started_by = events::MtuExchangeStarter::Central;
    value.claim_held = true;
    return value;
}

Traffic traffic() {
    Traffic value{};
    value.address = 0xDD1234;
    value.distance_m = 1800;
    value.bearing_deg = 315;
    value.relative_altitude_m = -42;
    value.closing_mps = 27;
    value.alarm = traffic::Level::Advisory;
    value.source = model::Source::AdslUplink;
    value.rssi_dbm = -88;
    value.tracked = 5;
    value.assessed = true;
    value.dismissed = true;
    value.in_formation = true;
    value.position_valid = true;
    value.relative_altitude_valid = true;
    return value;
}

Write write() {
    Write value{};
    value.waited_ms = 320;
    value.phase_ms = 30;
    value.requests = 6;
    value.writes = 4;
    value.forced = 1;
    value.placement = timing::DurableWriteVerdict::Place;
    value.kind = power::DurableWrite::FlightRecord;
    value.pending = true;
    return value;
}

Screen screen() {
    Screen value{};
    value.since_page_shown_ms = 4200;
    value.page = 2;
    value.mode = 1;
    value.prompt = 1;
    value.alarm = traffic::Level::Advisory;
    value.backlight = true;
    value.powered = true;
    value.holding = true;
    value.render_us = 8100;
    value.present_us = 21000;
    return value;
}

Gap gap() {
    Gap value{};
    value.dropped = 9;
    value.span_ms = 300;
    value.total = 12;
    value.capacity = 64;
    return value;
}

End end() {
    End value{};
    value.records = 4000;
    value.dropped = 9;
    return value;
}

Duty duty() {
    Duty value{};
    value.panel_partial_refreshes = 58;
    value.panel_full_refreshes = 2;
    value.backlight_ms = 3000;
    value.rx_armed_ms = 41000;
    value.tx_keyed_ms = 120;
    value.ble_connected_ms = 60000;
    value.annunciator_ms = 900;
    return value;
}

Switch hop() {
    Switch value{};
    value.to_hz = 868400000;
    value.margin_us = -150;
    value.took_us = 420;
    value.gap_us = 610;
    value.change = SwitchKind::Hop;
    value.armed_ahead = true;
    value.late = true;
    return value;
}

Loop loop() {
    Loop value{};
    value.passes = 980;
    value.busy_ms = 140;
    value.worst_pass_us = 23000;
    value.worst_pass_phase_ms = 812;
    value.worst_gap_ms = 31;
    value.worst_gap_phase_ms = 205;
    value.worst_service = LoopService::Screen;
    value.worst_tick_ms = 4;
    return value;
}

Adsb adsb() {
    Adsb value{};
    value.address = 0x4CA2D6;
    value.pressure_altitude_ft = 38000;
    value.height_difference_ft = -175;
    value.height_difference_age_ms = 1300;
    value.gnss_relative_altitude_m = 512;
    value.pressure_relative_altitude_m = 498;
    value.adsb_version = 2;
    value.gva = 2;
    value.adsb_version_valid = true;
    value.gva_valid = true;
    value.pressure_altitude_valid = true;
    value.height_difference_valid = true;
    value.gnss_relative_altitude_valid = true;
    value.pressure_relative_altitude_valid = true;
    return value;
}

std::vector<std::vector<uint8_t>> written() {
    const Instant at = vector_instant();
    return {bytes_of(record_of(boot(), at)),      bytes_of(record_of(config(), at)),
            bytes_of(record_of(gnss(), at)),      bytes_of(record_of(pps(), at)),
            bytes_of(record_of(burst())),         bytes_of(record_of(dwell(), at)),
            bytes_of(record_of(flight(), at)),    bytes_of(record_of(power(), at)),
            bytes_of(record_of(barometer(), at)), bytes_of(record_of(motion(), at)),
            bytes_of(record_of(contact(), at)),   bytes_of(record_of(link(), at)),
            bytes_of(record_of(traffic(), at)),   bytes_of(record_of(write(), at)),
            bytes_of(record_of(screen(), at)),    bytes_of(record_of(gap(), at)),
            bytes_of(record_of(end(), at)),       bytes_of(record_of(duty(), at)),
            bytes_of(record_of(hop(), at)),       bytes_of(record_of(loop(), at)),
            bytes_of(record_of(adsb(), at))};
}

}  // namespace

// scripts/test_blip.py decodes the same vectors to the schema's keys, so either side drifting
// fails.
TEST_CASE("diagnostics: every record type is written as the bytes the host decoder is held to") {
    const std::vector<std::vector<uint8_t>> pinned = HexVectors(kPath).frames("records");
    const std::vector<std::vector<uint8_t>> records = written();
    REQUIRE(pinned.size() == records.size());
    for (size_t i = 0; i < records.size(); i++) {
        CAPTURE(i);
        CHECK(records[i] == pinned[i]);
    }
}
