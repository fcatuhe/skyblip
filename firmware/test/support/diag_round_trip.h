// Harness, not a test: a payload goes through the 24 bytes it will be offloaded as, and comes back.
#ifndef SKYBLIP_TEST_SUPPORT_DIAG_ROUND_TRIP_H
#define SKYBLIP_TEST_SUPPORT_DIAG_ROUND_TRIP_H

#include "core/diag/payload.h"
#include "doctest/doctest.h"

namespace skyblip {

constexpr uint32_t kDiagTestUtcS = 1789000000;
constexpr uint16_t kDiagTestIntoMs = 462;

inline diag::Instant diag_test_instant() {
    diag::Instant at{};
    at.at_s = kDiagTestUtcS;
    at.into_ms = kDiagTestIntoMs;
    at.phase_valid = true;
    at.utc_dated = true;
    return at;
}

template <class T>
T diag_round_trip(const T& value) {
    const diag::Record in = diag::record_of(value, diag_test_instant());
    uint8_t raw[diag::kRecordBytes]{};
    diag::encode_record(in, raw);
    diag::Record decoded{};
    CHECK(diag::decode_record(raw, decoded) == Status::Ok);
    CHECK(decoded.at_s == kDiagTestUtcS);
    CHECK(decoded.into_ms == kDiagTestIntoMs);
    CHECK(decoded.phase_valid());
    CHECK(decoded.utc_dated());
    T out{};
    CHECK(diag::read(decoded, out));
    return out;
}

}  // namespace skyblip

#endif
