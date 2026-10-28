// Harness, not a test: a payload goes through the 24 bytes it will be offloaded as, and comes back.
#ifndef SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_ROUND_TRIP_H
#define SKYBLIP_TEST_SUPPORT_DIAGNOSTICS_ROUND_TRIP_H

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"

namespace skyblip {

constexpr uint32_t kDiagnosticsTestUtcS = 1789000000;
constexpr uint16_t kDiagnosticsTestIntoMs = 462;

inline diagnostics::Instant diagnostics_test_instant() {
    diagnostics::Instant at{};
    at.at_s = kDiagnosticsTestUtcS;
    at.into_ms = kDiagnosticsTestIntoMs;
    at.phase_valid = true;
    at.utc_dated = true;
    return at;
}

template <class T>
T diagnostics_round_trip(const T& value) {
    const diagnostics::Record in = diagnostics::record_of(value, diagnostics_test_instant());
    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(in, raw);
    diagnostics::Record decoded{};
    CHECK(diagnostics::decode_record(raw, decoded) == Status::Ok);
    CHECK(decoded.at_s == kDiagnosticsTestUtcS);
    CHECK(decoded.into_ms == kDiagnosticsTestIntoMs);
    CHECK(decoded.phase_valid());
    CHECK(decoded.utc_dated());
    T out{};
    CHECK(diagnostics::read(decoded, out));
    return out;
}

}  // namespace skyblip

#endif
