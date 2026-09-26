// The envelope: 24 bytes in the flight log's own geometry, and type tags a decoded corpus needs.
#include "core/diag/payload.h"
#include "core/diag/record.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "test/support/diag_round_trip.h"

using namespace skyblip;

TEST_CASE("diag record: one slot is the flight log's, so both share the sector ring") {
    CHECK(diag::kRecordBytes == flight::kLogRecordBytes);
    CHECK(diag::kPayloadOffset + diag::kPayloadBytes == diag::kRecordBytes);
}

TEST_CASE("diag record: the type tag is the corpus, and these numbers never move") {
    CHECK(static_cast<uint8_t>(diag::Type::None) == 0);
    CHECK(static_cast<uint8_t>(diag::Type::Boot) == 1);
    CHECK(static_cast<uint8_t>(diag::Type::Config) == 2);
    CHECK(static_cast<uint8_t>(diag::Type::Gnss) == 3);
    CHECK(static_cast<uint8_t>(diag::Type::Pps) == 4);
    CHECK(static_cast<uint8_t>(diag::Type::Burst) == 5);
    CHECK(static_cast<uint8_t>(diag::Type::Dwell) == 6);
    CHECK(static_cast<uint8_t>(diag::Type::Flight) == 7);
    CHECK(static_cast<uint8_t>(diag::Type::Power) == 8);
    CHECK(static_cast<uint8_t>(diag::Type::Baro) == 9);
    CHECK(static_cast<uint8_t>(diag::Type::Motion) == 10);
    CHECK(static_cast<uint8_t>(diag::Type::Contact) == 11);
    CHECK(static_cast<uint8_t>(diag::Type::Link) == 12);
    CHECK(static_cast<uint8_t>(diag::Type::Traffic) == 13);
    CHECK(static_cast<uint8_t>(diag::Type::Write) == 14);
    CHECK(static_cast<uint8_t>(diag::Type::Screen) == 15);
    CHECK(static_cast<uint8_t>(diag::Type::Gap) == 16);
    CHECK(static_cast<uint8_t>(diag::Type::End) == 17);
    CHECK(static_cast<uint8_t>(diag::Type::Duty) == 18);
    CHECK(diag::kHighestType == 18);
}

TEST_CASE("diag record: the header is written where a host decoder reads it") {
    diag::Gnss value{};
    value.nav_ms = 287;
    const diag::Record record = diag::record_of(value, diag_test_instant());
    uint8_t raw[diag::kRecordBytes]{};
    diag::encode_record(record, raw);

    CHECK(raw[0] == static_cast<uint8_t>(diag::Type::Gnss));
    CHECK((raw[1] & diag::kFlagPhaseValid) != 0);
    CHECK((raw[1] & diag::kFlagUtcDated) != 0);
    CHECK(raw[2] == 0xCE);
    CHECK(raw[3] == 0x01);
    CHECK(diag::get_u32(raw + 4) == kDiagTestUtc);
    CHECK(diag::get_u16(raw + diag::kPayloadOffset) == 287);
}

TEST_CASE("diag record: an erased slot is empty rather than a record of zeroes") {
    uint8_t raw[diag::kRecordBytes];
    for (uint8_t& byte : raw) byte = 0xFF;
    diag::Record out{};
    CHECK(diag::decode_record(raw, out) == Status::Empty);
}

TEST_CASE("diag record: a type this firmware never wrote is refused, not guessed at") {
    diag::Record out{};
    uint8_t raw[diag::kRecordBytes]{};
    raw[0] = diag::kHighestType + 1;
    CHECK(diag::decode_record(raw, out) == Status::Unsupported);
    raw[0] = 0;
    CHECK(diag::decode_record(raw, out) == Status::Unsupported);
}

TEST_CASE("diag record: a payload is read back only under its own type") {
    diag::Power value{};
    value.cell_mv = 3987;
    const diag::Record record = diag::record_of(value, diag_test_instant());
    diag::Baro wrong{};
    CHECK_FALSE(diag::read(record, wrong));
}

TEST_CASE("diag record: an instant with no PPS behind it says so rather than reading zero") {
    diag::Instant at{};
    at.at_s = 4120;
    at.into_ms = 0;
    const diag::Record record = diag::record_of(diag::Gnss{}, at);
    uint8_t raw[diag::kRecordBytes]{};
    diag::encode_record(record, raw);
    diag::Record decoded{};
    CHECK(diag::decode_record(raw, decoded) == Status::Ok);
    CHECK_FALSE(decoded.phase_valid());
    CHECK_FALSE(decoded.utc_dated());
    CHECK(decoded.at_s == 4120);
}

TEST_CASE("diag record: a stamp becomes an instant with the clock's own dating") {
    events::Stamp stamp{};
    stamp.at_s = kDiagTestUtc;
    stamp.into_ms = 812;
    stamp.phase_valid = true;
    const diag::Instant at = diag::instant_of(stamp, false);
    CHECK(at.at_s == kDiagTestUtc);
    CHECK(at.into_ms == 812);
    CHECK(at.phase_valid);
    CHECK_FALSE(at.utc_dated);
}
