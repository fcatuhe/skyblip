// The flight log's pure half: what a record is on flash, when a session runs,
// where the next one goes, and what the tablet is told. No device, no flash, no
// service - if any of this needs a board to be checked, it is in the wrong layer.
#include <cstring>

#include "core/flight/log_record.h"
#include "core/flight/log_session.h"
#include "core/model/ownship.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtc = 1785628800;  // 2026-08-02T00:00:00Z
constexpr uint8_t kParaglider = 7;         // ADS-L G.1.3

flight::LogRecord sample_record() {
    flight::LogRecord r{};
    r.utc_s = kBaseUtc + 1234;
    r.lat_1e7 = 485212345;
    r.lon_1e7 = -21234567;
    r.alt_msl_m = 1487;
    r.alt_hae_m = 1487 + 46;
    r.speed_q = 253;
    r.track_c9 = 401;
    r.climb_e8 = -37;
    r.hdop_e2 = 120;
    r.sats = 11;
    r.flight_state = 2;
    r.fix_valid = true;
    r.utc_valid = true;
    r.pps_locked = true;
    r.climb_valid = true;
    r.geoid_measured = true;
    return r;
}

model::Ownship flying(uint32_t utc_s, int32_t speed_mm_s) {
    model::Ownship own{};
    own.fix_valid = true;
    own.utc_valid = true;
    own.utc_s = utc_s;
    own.lat_1e7 = 485000000;
    own.lon_1e7 = 85000000;
    own.alt_hae_mm = 1000000;
    own.alt_msl_mm = 954000;
    own.speed_mm_s = speed_mm_s;
    own.track_cdeg = 9000;
    own.sats = 10;
    own.hdop_e2 = 100;
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Airborne);
    return own;
}

model::Ownship parked(uint32_t utc_s) {
    model::Ownship own = flying(utc_s, 0);
    own.flight_state = static_cast<uint8_t>(flight::FlightState::Ground);
    return own;
}

}  // namespace

TEST_CASE("log record: twenty-four bytes carry the instant own-ship publishes") {
    const flight::LogRecord in = sample_record();
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(in, kBaseUtc, raw);

    flight::LogRecord out{};
    REQUIRE(flight::decode_log_record(raw, kBaseUtc, out) == Status::Ok);
    CHECK(out.utc_s == in.utc_s);
    CHECK(out.lat_1e7 == in.lat_1e7);
    CHECK(out.lon_1e7 == in.lon_1e7);
    CHECK(out.alt_msl_m == in.alt_msl_m);
    // Both datums survive: the ellipsoidal height is stored as its distance from
    // mean sea level, so a reader gets back the pair own-ship published.
    CHECK(out.alt_hae_m == in.alt_hae_m);
    CHECK(out.speed_q == in.speed_q);
    CHECK(out.track_c9 == in.track_c9);
    CHECK(out.climb_e8 == in.climb_e8);
    CHECK(out.sats == in.sats);
    CHECK(out.hdop_e2 == in.hdop_e2);
    CHECK(out.flight_state == in.flight_state);
    CHECK(out.pps_locked);
    CHECK(out.geoid_measured);
    CHECK_FALSE(out.session_end);
}

TEST_CASE("log record: the budget the partition was sized on") {
    // 170 records and a 16-byte label in a 4 KB sector, to the byte.
    CHECK(flight::kLogSlotsPerSector == 170);
    CHECK(store::kSectorHeaderBytes + flight::kLogSlotsPerSector * flight::kLogRecordBytes ==
          flight::kLogSectorBytes);
    // A record a second puts 2 min 50 s in a sector: 14 h in log_partition's 298, 12 in a MiB.
    CHECK(flight::log_seconds_per_sector(flight::kLogSlotsPerSector) == 170);
    CHECK(flight::log_seconds_for(298, flight::kLogSlotsPerSector) == 50660);
    CHECK(
        flight::log_seconds_for(1024 * 1024 / flight::kLogSectorBytes, flight::kLogSlotsPerSector) /
            3600 ==
        12);
}

TEST_CASE("log record: an erased slot is empty and a torn one is a checksum failure") {
    uint8_t erased[flight::kLogRecordBytes];
    std::memset(erased, 0xFF, sizeof(erased));
    flight::LogRecord out{};
    // Never mistaken for a position, and never even asked the CRC: on NOR this
    // is what "nothing has been written here" looks like.
    CHECK(flight::decode_log_record(erased, kBaseUtc, out) == Status::Empty);

    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(sample_record(), kBaseUtc, raw);
    // The cell died halfway through the program: the bytes that made it are
    // real, the rest is still erased.
    for (size_t i = 9; i < sizeof(raw); i++) raw[i] = 0xFF;
    CHECK(flight::decode_log_record(raw, kBaseUtc, out) == Status::Crc);

    // And a single flipped bit anywhere in the payload is caught too.
    flight::encode_log_record(sample_record(), kBaseUtc, raw);
    raw[5] ^= 0x01;
    CHECK(flight::decode_log_record(raw, kBaseUtc, out) == Status::Crc);
}

TEST_CASE("log record: a session longer than the offset can count saturates rather than wraps") {
    flight::LogRecord in = sample_record();
    in.utc_s = kBaseUtc + 100000;
    uint8_t raw[flight::kLogRecordBytes];
    flight::encode_log_record(in, kBaseUtc, raw);
    flight::LogRecord out{};
    REQUIRE(flight::decode_log_record(raw, kBaseUtc, out) == Status::Ok);
    // Eighteen hours in, the record reads as eighteen hours in and not as the
    // start of the flight.
    CHECK(out.utc_s == kBaseUtc + 0xFFFF);
}

TEST_CASE("log session: a device parked on a trailer writes nothing") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 100; i++) {
        CHECK(session.update(parked(kBaseUtc + i), false, i * 1000) == flight::LogAction::Idle);
    }
    CHECK_FALSE(session.recording());
    // What it did keep is the last few seconds, in RAM, against a takeoff.
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);
}

// A paraglider soaring into wind reads on the ground for its whole flight.
TEST_CASE("log session: a wing that flies at no ground speed is logged while it moves") {
    flight::LogSession session;
    model::Ownship wing = parked(kBaseUtc);
    wing.aircraft_cat = kParaglider;
    CHECK(session.update(wing, false, 0) == flight::LogAction::Idle);

    wing.utc_s = kBaseUtc + 4;
    wing.speed_mm_s = 5000;
    CHECK(session.update(wing, true, 4000) == flight::LogAction::OpenSession);

    // Hovering over one spot is not a landing until it has lasted.
    wing.speed_mm_s = 0;
    const uint32_t stopped_ms = 4000 + flight::kLogStillHoldMs;
    for (uint32_t now_ms = 8000; now_ms < stopped_ms; now_ms += 4000) {
        wing.utc_s = kBaseUtc + now_ms / 1000;
        CHECK(session.update(wing, false, now_ms) != flight::LogAction::CloseSession);
    }
    CHECK(session.recording());
    wing.utc_s = kBaseUtc + stopped_ms / 1000;
    CHECK(session.update(wing, false, stopped_ms) == flight::LogAction::CloseSession);
}

TEST_CASE("log session: a parked wing and a taxiing aeroplane write nothing") {
    flight::LogSession session;
    model::Ownship wing = parked(kBaseUtc);
    wing.aircraft_cat = kParaglider;
    model::Ownship taxiing = parked(kBaseUtc);
    taxiing.speed_mm_s = 5000;
    for (uint32_t i = 0; i < 100; i++) {
        wing.utc_s = taxiing.utc_s = kBaseUtc + i;
        CHECK(session.update(wing, false, i * 1000) == flight::LogAction::Idle);
        CHECK(session.update(taxiing, true, i * 1000) == flight::LogAction::Idle);
    }
    CHECK_FALSE(session.recording());
}

TEST_CASE("log session: on the ground the ring is a holding pen and not a queue") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 4; i++) session.update(parked(kBaseUtc + i * 4), false, i * 4000);
    CHECK(session.queued() == 4);
    flight::LogRecord out{};
    // Nothing may be written out: this is the whole difference between a device
    // that keeps the last minute against a takeoff and a device that
    // fills its partition sitting in a trailer.
    CHECK_FALSE(session.peek(out));
}

TEST_CASE("log session: a record a second, the rate ADS-L reports an aircraft in flight at") {
    flight::LogSession session;
    session.update(flying(kBaseUtc, 50000), false, 0);
    flight::LogRecord drained{};
    while (session.peek(drained)) session.commit();

    CHECK(session.update(flying(kBaseUtc, 50000), false, 500) == flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc, 50000), false, 999) == flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc + 1, 50000), false, 1000) ==
          flight::LogAction::AppendRecord);
    CHECK(flight::kLogRecordPeriodMs == flight::kAirborneReportPeriodS * 1000);
}

// M. The record cadence across the 49.7-day wrap of
// ports::Clock::millis(). A flight log that stops sampling for seven weeks is a
// flight log with a hole in it that no badge claim survives, and the sampled_ flag
// beside the stamp is what keeps zero from meaning "never sampled" at the one
// instant the counter produces it.
TEST_CASE("log session: the record cadence spans the 49.7-day wrap") {
    flight::LogSession session;
    const uint32_t before = 0xFFFFFF00u;  // 256 ms short of the wrap
    session.update(flying(kBaseUtc, 50000), false, before);
    flight::LogRecord drained{};
    while (session.peek(drained)) session.commit();

    CHECK(session.update(flying(kBaseUtc, 50000), false, before + 500u) == flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc, 50000), false, before + 999u) == flight::LogAction::Idle);
    // 1000 ms after the last sample, which is 744 ms past zero.
    const uint32_t due = before + flight::kLogRecordPeriodMs;
    REQUIRE(due < before);  // the case is worthless unless it wrapped
    CHECK(session.update(flying(kBaseUtc + 1, 50000), false, due) ==
          flight::LogAction::AppendRecord);
    // And the cadence continues from there rather than from zero.
    CHECK(session.update(flying(kBaseUtc + 1, 50000), false, due + 999u) ==
          flight::LogAction::Idle);
    CHECK(session.update(flying(kBaseUtc + 2, 50000), false, due + 1000u) ==
          flight::LogAction::AppendRecord);
}

TEST_CASE("log session: the file opens before the criterion agreed, so the roll is in it") {
    flight::LogSession session;
    uint32_t now_ms = 0;
    // A minute of taxiing, sampled and held in RAM.
    for (uint32_t i = 0; i < 60; i++, now_ms += 1000)
        session.update(parked(kBaseUtc + i), false, now_ms);
    REQUIRE_FALSE(session.recording());

    CHECK(session.update(flying(kBaseUtc + 60, 50000), false, now_ms) ==
          flight::LogAction::OpenSession);
    CHECK(session.recording());
    // The session is named for the oldest sample still held, not for the instant
    // core/flight finally said "airborne" - 59 seconds of ground roll earlier.
    CHECK(session.session_id() == kBaseUtc + 1);
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);

    flight::LogRecord first{};
    REQUIRE(session.peek(first));
    session.commit();
    CHECK(first.utc_s == kBaseUtc + 1);
    CHECK(first.flight_state == static_cast<uint8_t>(flight::FlightState::Ground));
}

TEST_CASE("log session: a landing closes the session with a record that says so") {
    flight::LogSession session;
    uint32_t now_ms = 0;
    REQUIRE(session.update(flying(kBaseUtc, 50000), false, now_ms) ==
            flight::LogAction::OpenSession);
    flight::LogRecord drained{};
    while (session.peek(drained)) session.commit();

    now_ms += 4000;
    CHECK(session.update(parked(kBaseUtc + 4), false, now_ms) == flight::LogAction::CloseSession);
    CHECK_FALSE(session.recording());
    REQUIRE(session.peek(drained));
    session.commit();
    CHECK(drained.session_end);
    CHECK(drained.utc_s == kBaseUtc + 4);
}

// A writer that drains late used to have ground samples queued behind the
// landing record, so the last record of the file said the session was still
// open and a tablet listed the flight as unclosed.
TEST_CASE("log session: the landing record stays the last one while the writer is behind") {
    flight::LogSession session;
    uint32_t now_ms = 0;
    REQUIRE(session.update(flying(kBaseUtc, 50000), false, now_ms) ==
            flight::LogAction::OpenSession);
    flight::LogRecord drained{};
    while (session.peek(drained)) session.commit();

    now_ms += 4000;
    REQUIRE(session.update(parked(kBaseUtc + 4), false, now_ms) == flight::LogAction::CloseSession);
    CHECK(session.closing());
    // Half a minute of taxiing to the hangar, with nothing draining the ring.
    for (uint32_t i = 2; i < 10; i++)
        CHECK(session.update(parked(kBaseUtc + i * 4), false, now_ms + i * 4000) ==
              flight::LogAction::Idle);
    CHECK(session.queued() == 1);

    REQUIRE(session.peek(drained));
    session.commit();
    CHECK(drained.session_end);
    CHECK_FALSE(session.peek(drained));
    CHECK_FALSE(session.closing());
}

TEST_CASE("log session: a fix outage does not end the flight, and nothing is written across it") {
    flight::LogSession session;
    REQUIRE(session.update(flying(kBaseUtc, 50000), false, 0) == flight::LogAction::OpenSession);

    model::Ownship blind = flying(kBaseUtc + 4, 50000);
    blind.fix_valid = false;
    CHECK(session.update(blind, false, 4000) == flight::LogAction::Idle);
    CHECK(session.recording());

    // A row of zeroes is worse than a gap, so the gap is what the file gets.
    CHECK(session.update(flying(kBaseUtc + 8, 50000), false, 8000) ==
          flight::LogAction::AppendRecord);
    CHECK(session.session_id() == kBaseUtc);
}

TEST_CASE("log session: a writer that never drains loses the oldest, and says how many") {
    flight::LogSession session;
    for (uint32_t i = 0; i < 100; i++) session.update(flying(kBaseUtc + i, 50000), false, i * 1000);
    CHECK(session.queued() == flight::kLogPreTakeoffRecords);
    CHECK(session.dropped() == 100 - flight::kLogPreTakeoffRecords);
}

// The writer takes the record only once flash has it: a refused write must lose nothing.
TEST_CASE("log session: a record peeked and not committed is still in the ring") {
    flight::LogSession session;
    REQUIRE(session.update(flying(kBaseUtc, 50000), false, 0) == flight::LogAction::OpenSession);
    flight::LogRecord out{};
    REQUIRE(session.peek(out));
    CHECK(session.queued() == 1);
    REQUIRE(session.peek(out));
    CHECK(out.utc_s == kBaseUtc);

    session.commit();
    CHECK(session.queued() == 0);
    CHECK_FALSE(session.peek(out));
}

// A landing closes the file: what the aircraft did afterwards belongs to no flight.
TEST_CASE("log ring: the cursor walks the slots of a sector and stops at the last one") {
    flight::LogRing ring;
    ring.configure(3, 2);
    ring.restore(1, 0);
    CHECK(ring.sector() == 1);
    CHECK(ring.slot() == 0);
    CHECK_FALSE(ring.sector_exhausted());

    ring.took_slot();
    CHECK_FALSE(ring.sector_exhausted());
    ring.took_slot();
    CHECK(ring.sector_exhausted());

    // What recovery hands it: a frontier sector with no room left in it.
    ring.restore(2, ring.slots_per_sector());
    CHECK(ring.sector_exhausted());

    ring.rewind();
    CHECK(ring.sector() == 0);
    CHECK(ring.slot() == 0);
}
