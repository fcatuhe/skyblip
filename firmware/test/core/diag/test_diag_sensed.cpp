// What the device sensed: a fix, a PPS edge, a burst, the barometer, the hub, the cell, a thumb.
#include "core/diag/payload.h"
#include "doctest/doctest.h"
#include "test/support/diag_round_trip.h"

using namespace skyblip;

TEST_CASE("diag record: a fix carries where in its second it landed and why it was refused") {
    diag::Gnss in{};
    in.nav_ms = 287;
    in.resid_m = 41;
    in.hdop_e2 = 123;
    in.vdop_e2 = 211;
    in.stage_s = 4096;
    in.sats = 11;
    in.sats_in_view = 19;
    in.fix_mode = 3;
    in.reject = gnss::FixReject::Jump;
    in.stage = gnss::Stage::Solving;
    in.fix_valid = true;
    in.resid_valid = true;
    in.pps_locked = true;
    in.geoid_measured = true;
    in.tx_settled = true;
    in.levels_fresh = true;

    const diag::Gnss out = diag_round_trip(in);
    CHECK(out.nav_ms == in.nav_ms);
    CHECK(out.resid_m == in.resid_m);
    CHECK(out.hdop_e2 == in.hdop_e2);
    CHECK(out.vdop_e2 == in.vdop_e2);
    CHECK(out.stage_s == in.stage_s);
    CHECK(out.sats == in.sats);
    CHECK(out.sats_in_view == in.sats_in_view);
    CHECK(out.fix_mode == in.fix_mode);
    CHECK(out.reject == in.reject);
    CHECK(out.stage == in.stage);
    CHECK(out.fix_valid);
    CHECK(out.resid_valid);
    CHECK(out.pps_locked);
    CHECK(out.geoid_measured);
    CHECK(out.levels_fresh);
    CHECK(out.tx_settled);
}

TEST_CASE("diag record: a PPS edge keeps the sign of its error") {
    diag::Pps in{};
    in.interval_us = 999873;
    in.error_us = -127;
    in.samples = 3600;
    in.holdover_events = 2;
    in.since_edge_ms = 418;
    in.locked = true;
    in.utc_valid = true;

    const diag::Pps out = diag_round_trip(in);
    CHECK(out.interval_us == in.interval_us);
    CHECK(out.error_us == in.error_us);
    CHECK(out.samples == in.samples);
    CHECK(out.holdover_events == in.holdover_events);
    CHECK(out.since_edge_ms == in.since_edge_ms);
    CHECK(out.locked);
    CHECK(out.utc_valid);
}

TEST_CASE("diag record: a burst is the whole tape entry, instant and verdict together") {
    radio::Entry in{};
    in.event = radio::Event::Miskeyed;
    in.band = model::Band::M;
    in.source = model::Source::Alptas;
    in.addr = 0xDD8241;
    in.at_s = kDiagTestUtc;
    in.into_ms = 462;
    in.tx_keyed_us = 619;
    in.tx_span_us = 6014;
    in.rssi_dbm = -97;
    in.key_offset_s = -18;
    in.channel = 1;
    in.len = 26;
    in.addr_valid = true;
    in.rssi_valid = true;
    in.utc = true;
    in.airborne = true;
    in.phase_valid = true;
    in.tx_span_valid = true;
    in.callsign = true;

    const diag::Record record = diag::record_of(in);
    uint8_t raw[diag::kRecordBytes]{};
    diag::encode_record(record, raw);
    diag::Record decoded{};
    CHECK(diag::decode_record(raw, decoded) == Status::Ok);
    radio::Entry out{};
    CHECK(diag::read(decoded, out));

    CHECK(out.event == in.event);
    CHECK(out.band == in.band);
    CHECK(out.source == in.source);
    CHECK(out.addr == in.addr);
    CHECK(out.at_s == in.at_s);
    CHECK(out.into_ms == in.into_ms);
    CHECK(out.tx_keyed_us == in.tx_keyed_us);
    CHECK(out.tx_span_us == in.tx_span_us);
    CHECK(out.rssi_dbm == in.rssi_dbm);
    CHECK(out.key_offset_s == in.key_offset_s);
    CHECK(out.channel == in.channel);
    CHECK(out.len == in.len);
    CHECK(out.addr_valid);
    CHECK(out.rssi_valid);
    CHECK(out.utc);
    CHECK(out.airborne);
    CHECK(out.phase_valid);
    CHECK(out.tx_span_valid);
    CHECK(out.callsign);
}

// A name is nine characters and the payload has none free, so the flag and the address are it.
TEST_CASE("diag record: a burst that carried a callsign says so, and a position burst does not") {
    radio::Entry named{};
    named.event = radio::Event::Transmitted;
    named.callsign = true;
    radio::Entry position{};
    position.event = radio::Event::Transmitted;

    radio::Entry out{};
    CHECK(diag::read(diag::record_of(named), out));
    CHECK(out.callsign);
    CHECK(diag::read(diag::record_of(position), out));
    CHECK_FALSE(out.callsign);
}

TEST_CASE("diag record: every one of the twelve verdicts survives the slot") {
    for (uint8_t i = 0; i <= static_cast<uint8_t>(radio::Event::Unattempted); i++) {
        radio::Entry in{};
        in.event = static_cast<radio::Event>(i);
        uint8_t raw[diag::kRecordBytes]{};
        diag::encode_record(diag::record_of(in), raw);
        diag::Record decoded{};
        CHECK(diag::decode_record(raw, decoded) == Status::Ok);
        radio::Entry out{};
        CHECK(diag::read(decoded, out));
        CHECK(out.event == in.event);
    }
}

TEST_CASE("diag record: baro keeps the pressure beside the altitude derived from it") {
    diag::Baro in{};
    in.pressure_mpa = 101325000;
    in.alt_mm = -412;
    in.climb_mm_s = -2100;
    in.temperature_dc = 231;
    in.active = true;
    in.temperature_valid = true;
    in.climb_adopted = true;

    const diag::Baro out = diag_round_trip(in);
    CHECK(out.pressure_mpa == in.pressure_mpa);
    CHECK(out.alt_mm == in.alt_mm);
    CHECK(out.climb_mm_s == in.climb_mm_s);
    CHECK(out.temperature_dc == in.temperature_dc);
    CHECK(out.active);
    CHECK(out.temperature_valid);
    CHECK(out.climb_adopted);
}

TEST_CASE("diag record: motion keeps the ball and both ends of the g-meter") {
    diag::Motion in{};
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

    const diag::Motion out = diag_round_trip(in);
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

TEST_CASE("diag record: power keeps the cell, the verdict on it and the warnings counted") {
    diag::Power in{};
    in.cell_mv = 3987;
    in.supply_warnings = 3;
    in.implausible = 12;
    in.charge_warnings = 1;
    in.die_dc = -206;
    in.trim_offset_mv = -40;
    in.sample_offset_mv = -128;
    in.percent = 64;
    in.level = power::PowerLevel::Critical;
    in.charge = power::ChargeCondition::TooHot;
    in.charging = true;
    in.external_power = true;
    in.valid = true;
    in.die_valid = true;
    in.trim_learned = true;

    const diag::Power out = diag_round_trip(in);
    CHECK(out.cell_mv == in.cell_mv);
    CHECK(out.supply_warnings == in.supply_warnings);
    CHECK(out.implausible == in.implausible);
    CHECK(out.charge_warnings == in.charge_warnings);
    CHECK(out.die_dc == in.die_dc);
    CHECK(out.trim_offset_mv == in.trim_offset_mv);
    CHECK(out.sample_offset_mv == in.sample_offset_mv);
    CHECK(out.percent == in.percent);
    CHECK(out.level == in.level);
    CHECK(out.charge == in.charge);
    CHECK(out.charging);
    CHECK(out.external_power);
    CHECK(out.valid);
    CHECK(out.die_valid);
    CHECK(out.trim_learned);
}

TEST_CASE("diag record: the knee rides inside the level, and the bit it used to ride stays clear") {
    diag::Power in{};
    in.cell_mv = 3550;
    in.level = power::PowerLevel::Low;

    CHECK((diag::record_of(in, diag_test_instant()).flags & (1u << 6)) == 0);
    CHECK(diag_round_trip(in).level == power::PowerLevel::Low);
}

TEST_CASE("diag record: a contact keeps the instant the level moved, not the poll that saw it") {
    diag::Contact in{};
    in.at_ms = 4294967295u;
    in.held_ms = 1004;
    in.contact = events::Contact::Pad;
    in.gesture = 2;
    in.down = true;

    const diag::Contact out = diag_round_trip(in);
    CHECK(out.at_ms == in.at_ms);
    CHECK(out.held_ms == in.held_ms);
    CHECK(out.contact == in.contact);
    CHECK(out.gesture == in.gesture);
    CHECK(out.down);
}

TEST_CASE("diag record: a link event keeps the payload the central negotiated") {
    diag::Link in{};
    in.session = 41;
    in.payload_bytes = 182;
    in.frame_bytes = 96;
    in.holder = 41;
    in.drops = 3;
    in.action = diag::LinkAction::ClaimTaken;
    in.endpoint = events::Endpoint::Log;
    in.claim_held = true;

    const diag::Link out = diag_round_trip(in);
    CHECK(out.session == in.session);
    CHECK(out.payload_bytes == in.payload_bytes);
    CHECK(out.frame_bytes == in.frame_bytes);
    CHECK(out.holder == in.holder);
    CHECK(out.drops == in.drops);
    CHECK(out.action == in.action);
    CHECK(out.endpoint == in.endpoint);
    CHECK(out.claim_held);
}

TEST_CASE("diag record: a counter past what the field holds saturates rather than wrapping") {
    diag::Power in{};
    in.supply_warnings = 70000;
    in.implausible = 65535;
    const diag::Power out = diag_round_trip(in);
    CHECK(out.supply_warnings == 65535);
    CHECK(out.implausible == 65535);
}

TEST_CASE("diag record: a dwell change carries how long it took and how early it listened") {
    diag::Switch in{};
    in.to_hz = 868400000;
    in.margin_us = -2921;
    in.took_us = 5921;
    in.gap_us = 412;
    in.kind = diag::SwitchKind::Hop;
    in.armed_ahead = true;
    in.late = true;

    const diag::Switch out = diag_round_trip(in);
    CHECK(out.to_hz == in.to_hz);
    CHECK(out.margin_us == in.margin_us);
    CHECK(out.took_us == in.took_us);
    CHECK(out.gap_us == in.gap_us);
    CHECK(out.kind == in.kind);
    CHECK(out.armed_ahead);
    CHECK(out.late);
}
