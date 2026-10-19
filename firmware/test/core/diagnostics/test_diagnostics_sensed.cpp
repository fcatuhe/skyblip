// What the device sensed: a fix, a PPS edge, a burst, the barometer, the hub, the cell, a thumb.
#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_round_trip.h"

using namespace skyblip;

TEST_CASE(
    "diagnostics record: a fix carries where in its second it landed and why it was refused") {
    diagnostics::Gnss in{};
    in.solution_phase_ms = 287;
    in.residual_m = 41;
    in.hdop_e2 = 123;
    in.vdop_e2 = 211;
    in.stage_s = 4096;
    in.satellites = 11;
    in.satellites_in_view = 19;
    in.fix_mode = 3;
    in.reject = gnss::FixReject::Jump;
    in.stage = gnss::Stage::Solving;
    in.fix_valid = true;
    in.residual_valid = true;
    in.pps_locked = true;
    in.geoid_measured = true;
    in.tx_settled = true;
    in.levels_fresh = true;

    const diagnostics::Gnss out = diagnostics_round_trip(in);
    CHECK(out.solution_phase_ms == in.solution_phase_ms);
    CHECK(out.residual_m == in.residual_m);
    CHECK(out.hdop_e2 == in.hdop_e2);
    CHECK(out.vdop_e2 == in.vdop_e2);
    CHECK(out.stage_s == in.stage_s);
    CHECK(out.satellites == in.satellites);
    CHECK(out.satellites_in_view == in.satellites_in_view);
    CHECK(out.fix_mode == in.fix_mode);
    CHECK(out.reject == in.reject);
    CHECK(out.stage == in.stage);
    CHECK(out.fix_valid);
    CHECK(out.residual_valid);
    CHECK(out.pps_locked);
    CHECK(out.geoid_measured);
    CHECK(out.levels_fresh);
    CHECK(out.tx_settled);
}

TEST_CASE("diagnostics record: a PPS edge keeps the sign of its error") {
    diagnostics::Pps in{};
    in.interval_us = 999873;
    in.error_us = -127;
    in.samples = 3600;
    in.holdover_events = 2;
    in.since_edge_ms = 418;
    in.locked = true;
    in.utc_valid = true;

    const diagnostics::Pps out = diagnostics_round_trip(in);
    CHECK(out.interval_us == in.interval_us);
    CHECK(out.error_us == in.error_us);
    CHECK(out.samples == in.samples);
    CHECK(out.holdover_events == in.holdover_events);
    CHECK(out.since_edge_ms == in.since_edge_ms);
    CHECK(out.locked);
    CHECK(out.utc_valid);
}

TEST_CASE("diagnostics record: a burst is the whole tape entry, instant and verdict together") {
    radio::Entry in{};
    in.verdict = radio::Verdict::Miskeyed;
    in.band = model::Band::M;
    in.source = model::Source::Alptas;
    in.address = 0xDD8241;
    in.at_s = kDiagnosticsTestUtcS;
    in.into_ms = 462;
    in.tx_keyed_us = 619;
    in.tx_span_us = 6014;
    in.tx_stage_margin_us = -230;
    in.rssi_dbm = -97;
    in.key_offset_s = -18;
    in.channel = 1;
    in.bytes = 26;
    in.address_valid = true;
    in.rssi_valid = true;
    in.utc_dated = true;
    in.airborne = true;
    in.phase_valid = true;
    in.tx_span_valid = true;
    in.callsign = true;

    const diagnostics::Record record = diagnostics::record_of(in);
    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(record, raw);
    diagnostics::Record decoded{};
    CHECK(diagnostics::decode_record(raw, decoded) == Status::Ok);
    radio::Entry out{};
    CHECK(diagnostics::read(decoded, out));

    CHECK(out.verdict == in.verdict);
    CHECK(out.band == in.band);
    CHECK(out.source == in.source);
    CHECK(out.address == in.address);
    CHECK(out.at_s == in.at_s);
    CHECK(out.into_ms == in.into_ms);
    CHECK(out.tx_keyed_us == in.tx_keyed_us);
    CHECK(out.tx_span_us == in.tx_span_us);
    CHECK(out.tx_stage_margin_us == in.tx_stage_margin_us);
    CHECK(out.rssi_dbm == in.rssi_dbm);
    CHECK(out.key_offset_s == in.key_offset_s);
    CHECK(out.channel == in.channel);
    CHECK(out.bytes == in.bytes);
    CHECK(out.address_valid);
    CHECK(out.rssi_valid);
    CHECK(out.utc_dated);
    CHECK(out.airborne);
    CHECK(out.phase_valid);
    CHECK(out.tx_span_valid);
    CHECK(out.callsign);
}

// A name is nine characters and the payload has none free, so the flag and the address are it.
TEST_CASE(
    "diagnostics record: a burst that carried a callsign says so, and a position burst does not") {
    radio::Entry named{};
    named.verdict = radio::Verdict::Transmitted;
    named.callsign = true;
    radio::Entry position{};
    position.verdict = radio::Verdict::Transmitted;

    radio::Entry out{};
    CHECK(diagnostics::read(diagnostics::record_of(named), out));
    CHECK(out.callsign);
    CHECK(diagnostics::read(diagnostics::record_of(position), out));
    CHECK_FALSE(out.callsign);
}

TEST_CASE("diagnostics record: every one of the twelve verdicts survives the slot") {
    for (uint8_t i = 0; i <= static_cast<uint8_t>(radio::Verdict::Unattempted); i++) {
        radio::Entry in{};
        in.verdict = static_cast<radio::Verdict>(i);
        uint8_t raw[diagnostics::kRecordBytes]{};
        diagnostics::encode_record(diagnostics::record_of(in), raw);
        diagnostics::Record decoded{};
        CHECK(diagnostics::decode_record(raw, decoded) == Status::Ok);
        radio::Entry out{};
        CHECK(diagnostics::read(decoded, out));
        CHECK(out.verdict == in.verdict);
    }
}

TEST_CASE("diagnostics record: baro keeps the pressure beside the altitude derived from it") {
    diagnostics::Barometer in{};
    in.pressure_mpa = 101325000;
    in.altitude_mm = -412;
    in.climb_mm_s = -2100;
    in.temperature_dc = 231;
    in.active = true;
    in.temperature_valid = true;
    in.climb_adopted = true;

    const diagnostics::Barometer out = diagnostics_round_trip(in);
    CHECK(out.pressure_mpa == in.pressure_mpa);
    CHECK(out.altitude_mm == in.altitude_mm);
    CHECK(out.climb_mm_s == in.climb_mm_s);
    CHECK(out.temperature_dc == in.temperature_dc);
    CHECK(out.active);
    CHECK(out.temperature_valid);
    CHECK(out.climb_adopted);
}

TEST_CASE("diagnostics record: motion keeps the ball and both ends of the g-meter") {
    diagnostics::Motion in{};
    in.slip_mg = -87;
    in.normal_mg = 1620;
    in.lateral_mg = -140;
    in.longitudinal_mg = 55;
    in.most_normal_mg = 3210;
    in.least_normal_mg = -420;
    in.imu_error = 0x21;
    in.sensor_error = 0x07;
    in.slip_valid = true;
    in.gload_valid = true;
    in.fitted = true;

    const diagnostics::Motion out = diagnostics_round_trip(in);
    CHECK(out.slip_mg == in.slip_mg);
    CHECK(out.normal_mg == in.normal_mg);
    CHECK(out.lateral_mg == in.lateral_mg);
    CHECK(out.longitudinal_mg == in.longitudinal_mg);
    CHECK(out.most_normal_mg == in.most_normal_mg);
    CHECK(out.least_normal_mg == in.least_normal_mg);
    CHECK(out.imu_error == in.imu_error);
    CHECK(out.sensor_error == in.sensor_error);
    CHECK(out.slip_valid);
    CHECK(out.gload_valid);
    CHECK(out.fitted);
}

TEST_CASE("diagnostics record: power keeps the cell, the verdict on it and the warnings counted") {
    diagnostics::Power in{};
    in.cell_mv = 3987;
    in.supply_warnings = 3;
    in.implausible = 12;
    in.charge_warnings = 1;
    in.die_dc = -206;
    in.sample_offset_mv = -128;
    in.percent = 64;
    in.level = power::PowerLevel::Critical;
    in.charge = power::ChargeCondition::TooHot;
    in.charging = true;
    in.external_power = true;
    in.valid = true;
    in.die_valid = true;

    const diagnostics::Power out = diagnostics_round_trip(in);
    CHECK(out.cell_mv == in.cell_mv);
    CHECK(out.supply_warnings == in.supply_warnings);
    CHECK(out.implausible == in.implausible);
    CHECK(out.charge_warnings == in.charge_warnings);
    CHECK(out.die_dc == in.die_dc);
    CHECK(out.sample_offset_mv == in.sample_offset_mv);
    CHECK(out.percent == in.percent);
    CHECK(out.level == in.level);
    CHECK(out.charge == in.charge);
    CHECK(out.charging);
    CHECK(out.external_power);
    CHECK(out.valid);
    CHECK(out.die_valid);
}

TEST_CASE(
    "diagnostics record: the knee rides inside the level, and the bit it used to ride stays "
    "clear") {
    diagnostics::Power in{};
    in.cell_mv = 3550;
    in.level = power::PowerLevel::Low;

    CHECK((diagnostics::record_of(in, diagnostics_test_instant()).flags & (1u << 6)) == 0);
    CHECK(diagnostics_round_trip(in).level == power::PowerLevel::Low);
}

TEST_CASE("diagnostics record: the retired trim bit and bytes are written clear and never reused") {
    diagnostics::Power in{};
    in.cell_mv = 4812;
    in.sample_offset_mv = -5;
    in.external_power = true;
    in.charging = true;

    const diagnostics::Record record = diagnostics::record_of(in, diagnostics_test_instant());
    CHECK((record.flags & (1u << 7)) == 0);
    CHECK(record.payload[13] == 0);
    CHECK(record.payload[14] == 0);
}

TEST_CASE(
    "diagnostics record: a contact keeps the instant the level moved, not the poll that saw it") {
    diagnostics::Contact in{};
    in.at_ms = 4294967295u;
    in.held_ms = 1004;
    in.contact = events::Contact::Pad;
    in.gesture = 2;
    in.down = true;

    const diagnostics::Contact out = diagnostics_round_trip(in);
    CHECK(out.at_ms == in.at_ms);
    CHECK(out.held_ms == in.held_ms);
    CHECK(out.contact == in.contact);
    CHECK(out.gesture == in.gesture);
    CHECK(out.down);
}

TEST_CASE("diagnostics record: a link event keeps the payload the central negotiated") {
    diagnostics::Link in{};
    in.link_session = 41;
    in.payload_bytes = 182;
    in.frame_bytes = 96;
    in.holder = 41;
    in.drops = 3;
    in.action = diagnostics::LinkAction::ClaimTaken;
    in.endpoint = events::Endpoint::Log;
    in.claim_held = true;

    const diagnostics::Link out = diagnostics_round_trip(in);
    CHECK(out.link_session == in.link_session);
    CHECK(out.payload_bytes == in.payload_bytes);
    CHECK(out.frame_bytes == in.frame_bytes);
    CHECK(out.holder == in.holder);
    CHECK(out.drops == in.drops);
    CHECK(out.action == in.action);
    CHECK(out.endpoint == in.endpoint);
    CHECK(out.claim_held);
}

TEST_CASE(
    "diagnostics record: a counter past what the field holds saturates rather than wrapping") {
    diagnostics::Power in{};
    in.supply_warnings = 70000;
    in.implausible = 65535;
    const diagnostics::Power out = diagnostics_round_trip(in);
    CHECK(out.supply_warnings == 65535);
    CHECK(out.implausible == 65535);
}

TEST_CASE("diagnostics record: a dwell change carries how long it took and how early it listened") {
    diagnostics::Switch in{};
    in.to_hz = 868400000;
    in.margin_us = -2921;
    in.took_us = 5921;
    in.gap_us = 412;
    in.change = diagnostics::SwitchKind::Hop;
    in.armed_ahead = true;
    in.late = true;

    const diagnostics::Switch out = diagnostics_round_trip(in);
    CHECK(out.to_hz == in.to_hz);
    CHECK(out.margin_us == in.margin_us);
    CHECK(out.took_us == in.took_us);
    CHECK(out.gap_us == in.gap_us);
    CHECK(out.change == in.change);
    CHECK(out.armed_ahead);
    CHECK(out.late);
}

// The byte holds 10 us steps, so a margin past +-1.27 ms reads as the ceiling and not as noise.
TEST_CASE("diagnostics record: a stage margin past what the byte holds saturates at its ends") {
    radio::Entry late{};
    late.tx_stage_margin_us = -4000;
    radio::Entry early{};
    early.tx_stage_margin_us = 1999;

    radio::Entry out{};
    CHECK(diagnostics::read(diagnostics::record_of(late), out));
    CHECK(out.tx_stage_margin_us == -1280);
    CHECK(diagnostics::read(diagnostics::record_of(early), out));
    CHECK(out.tx_stage_margin_us == 1270);
}
