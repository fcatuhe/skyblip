// What a flight leaves beside its track: the cell, the alarms and every aircraft's closest pass.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fec/crc.h"
#include "core/flight/log_events.h"
#include "core/flight/log_record.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtc = 1785628800;  // 2026-08-02T00:00:00Z

flight::LogStatus cell(uint16_t mv, uint8_t level, uint8_t tracked) {
    flight::LogStatus s{};
    s.cell_mv = mv;
    s.percent = 64;
    s.power_level = level;
    s.charge = 1;
    s.tracked = tracked;
    s.battery_valid = true;
    return s;
}

flight::LogContact glider(uint32_t address, uint16_t dist_m, uint8_t alarm) {
    flight::LogContact c{};
    c.address = address;
    c.address_mapping_table = 6;
    c.aircraft_cat = 2;
    c.dist_m = dist_m;
    c.rel_alt_m = -42;
    c.rel_bearing_deg = 315;
    c.closing_mps = 12;
    c.alarm = alarm;
    c.source = 0;
    c.rssi_dbm = -88;
    return c;
}

std::string hex(const uint8_t* raw) {
    std::string out;
    char pair[3];
    for (uint32_t i = 0; i < flight::kLogRecordBytes; i++) {
        std::snprintf(pair, sizeof(pair), "%02x", raw[i]);
        out += pair;
    }
    return out;
}

void reseal(uint8_t* raw) {
    const uint16_t crc = fec::crc16_ccitt(raw, flight::kLogRecordBytes - 2);
    raw[22] = static_cast<uint8_t>(crc);
    raw[23] = static_cast<uint8_t>(crc >> 8);
}

int drain(flight::LogEvents& events, flight::LogEvent* out, int cap) {
    int n = 0;
    flight::LogEvent event{};
    while (n < cap && events.peek(event)) {
        out[n++] = event;
        events.commit();
    }
    return n;
}

}  // namespace

TEST_CASE("log event: a status and an encounter come back from twenty-four bytes as written") {
    flight::LogEvent status{};
    status.kind = flight::LogKind::Status;
    status.utc = kBaseUtc + 61;
    status.status = cell(3912, 1, 3);
    status.status.tracked_peak = 5;
    status.status.charging = true;
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_event(status, kBaseUtc, raw);

    flight::LogEvent back{};
    REQUIRE(flight::decode_log_event(raw, kBaseUtc, back) == Status::Ok);
    CHECK(back.kind == flight::LogKind::Status);
    CHECK(back.utc == kBaseUtc + 61);
    CHECK(back.status.cell_mv == 3912);
    CHECK(back.status.percent == 64);
    CHECK(back.status.power_level == 1);
    CHECK(back.status.tracked == 3);
    CHECK(back.status.tracked_peak == 5);
    CHECK(back.status.battery_valid);
    CHECK(back.status.charging);
    CHECK_FALSE(back.status.external_power);

    flight::LogEvent pass{};
    pass.kind = flight::LogKind::Encounter;
    pass.utc = kBaseUtc + 300;
    pass.contact = glider(0xDD1234, 180, 1);
    pass.contact.heard_s = 95;
    pass.contact.in_formation = true;
    flight::encode_log_event(pass, kBaseUtc, raw);
    REQUIRE(flight::decode_log_event(raw, kBaseUtc, back) == Status::Ok);
    CHECK(back.kind == flight::LogKind::Encounter);
    CHECK(back.utc == kBaseUtc + 300);
    CHECK(back.contact.address == 0xDD1234);
    CHECK(back.contact.address_mapping_table == 6);
    CHECK(back.contact.aircraft_cat == 2);
    CHECK(back.contact.dist_m == 180);
    CHECK(back.contact.rel_alt_m == -42);
    CHECK(back.contact.rel_bearing_deg == 315);
    CHECK(back.contact.closing_mps == 12);
    CHECK(back.contact.alarm == 1);
    CHECK(back.contact.rssi_dbm == -88);
    CHECK(back.contact.heard_s == 95);
    CHECK(back.contact.in_formation);
    CHECK_FALSE(back.contact.dismissed);
}

TEST_CASE("log event: a fix is never read as an event, nor an event as a fix") {
    flight::LogRecord fix{};
    fix.utc = kBaseUtc + 5;
    fix.track_c9 = 511;
    fix.fix_valid = true;
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(fix, kBaseUtc, raw);
    flight::LogEvent event{};
    flight::LogRecord record{};
    CHECK(flight::decode_log_event(raw, kBaseUtc, event) == Status::Unsupported);
    CHECK(flight::decode_log_record(raw, kBaseUtc, record) == Status::Ok);

    flight::LogEvent alarm{};
    alarm.kind = flight::LogKind::Alarm;
    alarm.utc = kBaseUtc + 5;
    alarm.contact = glider(0x3F0001, 900, 1);
    flight::encode_log_event(alarm, kBaseUtc, raw);
    CHECK(flight::decode_log_record(raw, kBaseUtc, record) == Status::Unsupported);
    CHECK(flight::decode_log_event(raw, kBaseUtc, event) == Status::Ok);

    // A kind a later build added, under a checksum that holds, is not guessed at.
    raw[flight::kLogKindOffset] = flight::kLogEventMark | 0x7F;
    reseal(raw);
    CHECK(flight::decode_log_event(raw, kBaseUtc, event) == Status::Unsupported);
    CHECK(flight::decode_log_record(raw, kBaseUtc, record) == Status::Unsupported);

    flight::encode_log_event(alarm, kBaseUtc, raw);
    for (uint32_t i = 9; i < sizeof(raw); i++) raw[i] = 0xFF;
    CHECK(flight::decode_log_event(raw, kBaseUtc, event) == Status::Crc);
    std::memset(raw, 0xFF, sizeof(raw));
    CHECK(flight::decode_log_event(raw, kBaseUtc, event) == Status::Empty);
}

// scripts/test_blip.py decodes these same bytes: the two layouts are one, written twice.
TEST_CASE("log event: the bytes the host decoder is pinned to") {
    uint8_t raw[flight::kLogRecordBytes];
    flight::LogEvent status{};
    status.kind = flight::LogKind::Status;
    status.utc = kBaseUtc + 61;
    status.status = cell(3912, 4, 3);
    status.status.tracked_peak = 5;
    status.status.external_power = true;
    flight::encode_log_event(status, kBaseUtc, raw);
    CHECK(hex(raw) == "3d00480f400401030505000000000000810000000000b84d");

    flight::LogEvent pass{};
    pass.kind = flight::LogKind::Encounter;
    pass.utc = kBaseUtc + 300;
    pass.contact = glider(0xDD1234, 180, 1);
    pass.contact.heard_s = 95;
    flight::encode_log_event(pass, kBaseUtc, raw);
    CHECK(hex(raw) == "2c013412dd06b400d6ff3b010c0001028300a8005f002eb1");
}

TEST_CASE("log events: a status opens the flight, then one a minute, and one when the cell drops") {
    flight::LogEvents events;
    events.open();
    flight::LogEvent out[8];

    events.status(cell(4000, 1, 2), kBaseUtc, 1000);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].kind == flight::LogKind::Status);
    CHECK(out[0].utc == kBaseUtc);

    events.status(cell(3990, 1, 7), kBaseUtc + 30, 31000);
    CHECK(drain(events, out, 8) == 0);
    events.status(cell(3980, 1, 4), kBaseUtc + 60, 1000 + flight::kLogStatusPeriodMs);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].status.tracked == 4);
    // The most aircraft heard at once since the last status, not the count at this second.
    CHECK(out[0].status.tracked_peak == 7);

    events.status(cell(3500, 4, 4), kBaseUtc + 75, 76000);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].status.power_level == 4);
    CHECK(out[0].utc == kBaseUtc + 75);
}

TEST_CASE("log events: an alarm is written when a contact's level changes, rising and falling") {
    flight::LogEvents events;
    events.open();
    flight::LogEvent out[8];

    events.heard(3, glider(0xABCDEF, 4000, 0), kBaseUtc + 1);
    CHECK(drain(events, out, 8) == 0);
    events.heard(3, glider(0xABCDEF, 2500, 1), kBaseUtc + 2);
    events.heard(3, glider(0xABCDEF, 2000, 1), kBaseUtc + 3);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].kind == flight::LogKind::Alarm);
    CHECK(out[0].utc == kBaseUtc + 2);
    CHECK(out[0].contact.alarm == 1);
    CHECK(out[0].contact.dist_m == 2500);

    events.heard(3, glider(0xABCDEF, 3500, 0), kBaseUtc + 9);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].kind == flight::LogKind::Alarm);
    CHECK(out[0].contact.alarm == 0);
}

TEST_CASE("log events: an encounter is written once, at its closest, when the aircraft leaves") {
    flight::LogEvents events;
    events.open();
    flight::LogEvent out[8];

    const uint16_t passes[] = {4000, 2600, 900, 350, 600, 3100};
    for (uint32_t i = 0; i < 6; i++)
        events.heard(7, glider(0x111111, passes[i], passes[i] <= 3000 ? 1 : 0), kBaseUtc + 10 + i);
    drain(events, out, 8);
    events.vacant(7);

    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].kind == flight::LogKind::Encounter);
    CHECK(out[0].utc == kBaseUtc + 13);
    CHECK(out[0].contact.dist_m == 350);
    CHECK(out[0].contact.alarm == 1);
    CHECK(out[0].contact.heard_s == 5);
    events.vacant(7);
    CHECK(drain(events, out, 8) == 0);
}

TEST_CASE("log events: a slot handed to another aircraft ends the first one's encounter") {
    flight::LogEvents events;
    events.open();
    flight::LogEvent out[8];

    events.heard(0, glider(0x000AAA, 1500, 0), kBaseUtc);
    events.heard(0, glider(0x000BBB, 2500, 0), kBaseUtc + 1);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].contact.address == 0x000AAA);
    events.vacant(0);
    REQUIRE(drain(events, out, 8) == 1);
    CHECK(out[0].contact.address == 0x000BBB);
}

TEST_CASE("log events: a landing ends every encounter still running, after what was queued") {
    flight::LogEvents events;
    events.open();
    flight::LogEvent out[64];

    for (int slot = 0; slot < flight::kLogContactSlots; slot++)
        events.heard(slot, glider(0x100000 + static_cast<uint32_t>(slot), 5000, 0), kBaseUtc);
    events.heard(5, glider(0x100005, 2000, 1), kBaseUtc + 1);
    events.close();

    REQUIRE(drain(events, out, 64) == 1 + flight::kLogContactSlots);
    CHECK(out[0].kind == flight::LogKind::Alarm);
    for (int i = 1; i <= flight::kLogContactSlots; i++)
        CHECK(out[i].kind == flight::LogKind::Encounter);
    CHECK(events.dropped() == 0);

    events.heard(1, glider(0x200000, 100, 1), kBaseUtc + 99);
    events.status(cell(4000, 1, 1), kBaseUtc + 99, 99000);
    CHECK(drain(events, out, 64) == 0);
}

TEST_CASE("log events: a backlog nobody drains loses the oldest, and says how many") {
    flight::LogEvents events;
    events.open();
    for (int i = 0; i < flight::kLogEventBacklogRecords + 5; i++)
        events.heard(0, glider(0x300000 + static_cast<uint32_t>(i), 500, 1),
                     kBaseUtc + static_cast<uint32_t>(i));
    CHECK(events.dropped() > 0);
    flight::LogEvent out[64];
    CHECK(drain(events, out, 64) == flight::kLogEventBacklogRecords);
}
