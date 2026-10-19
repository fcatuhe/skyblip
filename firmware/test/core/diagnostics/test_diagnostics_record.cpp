// The envelope: 24 bytes in the flight log's own geometry, and type tags a decoded corpus needs.
#include "core/diagnostics/payload.h"
#include "core/diagnostics/record.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_round_trip.h"

using namespace skyblip;

TEST_CASE("diag record: one slot is the flight log's, so both share the sector ring") {
    CHECK(diagnostics::kRecordBytes == flight::kLogRecordBytes);
    CHECK(diagnostics::kPayloadOffset + diagnostics::kPayloadBytes == diagnostics::kRecordBytes);
}

TEST_CASE("diag record: the type tag is the corpus, and these numbers never move") {
    CHECK(static_cast<uint8_t>(diagnostics::Type::None) == 0);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Boot) == 1);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Config) == 2);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Gnss) == 3);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Pps) == 4);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Burst) == 5);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Dwell) == 6);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Flight) == 7);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Power) == 8);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Barometer) == 9);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Motion) == 10);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Contact) == 11);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Link) == 12);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Traffic) == 13);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Write) == 14);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Screen) == 15);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Gap) == 16);
    CHECK(static_cast<uint8_t>(diagnostics::Type::End) == 17);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Duty) == 18);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Switch) == 19);
    CHECK(static_cast<uint8_t>(diagnostics::Type::Loop) == 20);
    CHECK(diagnostics::kHighestType == 20);
}

TEST_CASE("diag record: the header is written where a host decoder reads it") {
    diagnostics::Gnss value{};
    value.nav_ms = 287;
    const diagnostics::Record record = diagnostics::record_of(value, diagnostics_test_instant());
    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(record, raw);

    CHECK(raw[0] == static_cast<uint8_t>(diagnostics::Type::Gnss));
    CHECK((raw[1] & diagnostics::kFlagPhaseValid) != 0);
    CHECK((raw[1] & diagnostics::kFlagUtcDated) != 0);
    CHECK(raw[2] == 0xCE);
    CHECK(raw[3] == 0x01);
    CHECK(get_u32(raw + 4) == kDiagnosticsTestUtcS);
    CHECK(get_u16(raw + diagnostics::kPayloadOffset) == 287);
}

TEST_CASE("diag record: an erased slot is empty rather than a record of zeroes") {
    uint8_t raw[diagnostics::kRecordBytes];
    for (uint8_t& byte : raw) byte = 0xFF;
    diagnostics::Record out{};
    CHECK(diagnostics::decode_record(raw, out) == Status::Empty);
}

TEST_CASE("diag record: a type this firmware never wrote is refused, not guessed at") {
    diagnostics::Record out{};
    uint8_t raw[diagnostics::kRecordBytes]{};
    raw[0] = diagnostics::kHighestType + 1;
    CHECK(diagnostics::decode_record(raw, out) == Status::Unsupported);
    raw[0] = 0;
    CHECK(diagnostics::decode_record(raw, out) == Status::Unsupported);
}

TEST_CASE("diag record: a payload is read back only under its own type") {
    diagnostics::Power value{};
    value.cell_mv = 3987;
    const diagnostics::Record record = diagnostics::record_of(value, diagnostics_test_instant());
    diagnostics::Barometer wrong{};
    CHECK_FALSE(diagnostics::read(record, wrong));
}

TEST_CASE("diag record: an instant with no PPS behind it says so rather than reading zero") {
    diagnostics::Instant at{};
    at.at_s = 4120;
    at.into_ms = 0;
    const diagnostics::Record record = diagnostics::record_of(diagnostics::Gnss{}, at);
    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(record, raw);
    diagnostics::Record decoded{};
    CHECK(diagnostics::decode_record(raw, decoded) == Status::Ok);
    CHECK_FALSE(decoded.phase_valid());
    CHECK_FALSE(decoded.utc_dated());
    CHECK(decoded.at_s == 4120);
}

TEST_CASE("diag record: a stamp becomes an instant with the clock's own dating") {
    events::Stamp stamp{};
    stamp.at_s = kDiagnosticsTestUtcS;
    stamp.into_ms = 812;
    stamp.phase_valid = true;
    const diagnostics::Instant at = diagnostics::instant_of(stamp, false);
    CHECK(at.at_s == kDiagnosticsTestUtcS);
    CHECK(at.into_ms == 812);
    CHECK(at.phase_valid);
    CHECK_FALSE(at.utc_dated);
}
