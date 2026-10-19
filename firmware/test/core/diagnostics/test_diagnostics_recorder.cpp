// The RAM ring a capture fills: disarmed at boot, drained by the writer, honest about every drop.
#include "core/diagnostics/recorder.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtcS = 1789000000;

diagnostics::Instant at_ms(uint32_t millis) {
    diagnostics::Instant at{};
    at.at_s = kBaseUtcS + millis / 1000;
    at.into_ms = static_cast<uint16_t>(millis % 1000);
    at.phase_valid = true;
    at.utc_dated = true;
    return at;
}

diagnostics::Gnss fix(uint8_t satellites) {
    diagnostics::Gnss value{};
    value.satellites = satellites;
    value.fix_valid = true;
    return value;
}

bool took(diagnostics::Recorder& recorder, diagnostics::Record& out) {
    if (!recorder.peek(out)) return false;
    recorder.commit();
    return true;
}

int drain(diagnostics::Recorder& recorder) {
    diagnostics::Record record{};
    int taken = 0;
    while (took(recorder, record)) taken++;
    return taken;
}

}  // namespace

TEST_CASE("diag recorder: a boot comes up disarmed and records nothing") {
    diagnostics::Recorder recorder{};
    CHECK_FALSE(recorder.armed());
    CHECK_FALSE(recorder.record(fix(7), at_ms(0)));
    CHECK(recorder.queued() == 0);
    CHECK(recorder.written() == 0);
    CHECK(recorder.dropped() == 0);
    diagnostics::Record record{};
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: disarming stops the capture and leaves the tail drainable") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    CHECK(recorder.record(fix(7), at_ms(0)));
    recorder.disarm();
    CHECK_FALSE(recorder.record(fix(8), at_ms(1000)));
    CHECK(recorder.queued() == 1);
    diagnostics::Record record{};
    CHECK(took(recorder, record));
    diagnostics::Gnss out{};
    CHECK(diagnostics::read(record, out));
    CHECK(out.satellites == 7);
}

TEST_CASE("diag recorder: arming again after a disarm starts a clean corpus") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords + 4; i++)
        recorder.record(fix(1), at_ms(i));
    CHECK(recorder.dropped() == 4);
    recorder.disarm();
    recorder.arm();
    CHECK(recorder.queued() == 0);
    CHECK(recorder.dropped() == 0);
    CHECK(recorder.written() == 0);
}

TEST_CASE("diag recorder: records come back in the order they were taken") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (uint8_t i = 0; i < 5; i++) CHECK(recorder.record(fix(i), at_ms(i)));

    for (uint8_t i = 0; i < 5; i++) {
        diagnostics::Record record{};
        CHECK(took(recorder, record));
        diagnostics::Gnss out{};
        CHECK(diagnostics::read(record, out));
        CHECK(out.satellites == i);
    }
    diagnostics::Record record{};
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: a burst carries its own instant off the tape") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    radio::Entry entry{};
    entry.verdict = radio::Verdict::Transmitted;
    entry.at_s = kBaseUtcS;
    entry.into_ms = 462;
    entry.phase_valid = true;
    entry.utc_dated = true;
    CHECK(recorder.record(entry));

    diagnostics::Record record{};
    CHECK(took(recorder, record));
    CHECK(record.type == diagnostics::Type::Burst);
    CHECK(record.at_s == kBaseUtcS);
    CHECK(record.into_ms == 462);
}

TEST_CASE("diag recorder: a full ring refuses the new record rather than the history") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++)
        CHECK(recorder.record(fix(3), at_ms(i)));
    CHECK_FALSE(recorder.record(fix(9), at_ms(1000)));
    CHECK(recorder.dropped() == 1);
    CHECK(recorder.queued() == diagnostics::Recorder::kMaxRecords);

    diagnostics::Record record{};
    CHECK(took(recorder, record));
    diagnostics::Gnss out{};
    CHECK(diagnostics::read(record, out));
    CHECK(out.satellites == 3);
}

TEST_CASE("diag recorder: the hole is a gap record where the hole is") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++) recorder.record(fix(1), at_ms(i));
    recorder.record(fix(2), at_ms(2000));
    recorder.record(fix(2), at_ms(2400));
    recorder.record(fix(2), at_ms(3100));
    CHECK(recorder.dropped() == 3);

    diagnostics::Record record{};
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++) {
        CHECK(took(recorder, record));
        CHECK(record.type == diagnostics::Type::Gnss);
    }
    CHECK(took(recorder, record));
    CHECK(record.type == diagnostics::Type::Gap);
    CHECK(record.at_s == kBaseUtcS + 2);
    CHECK(record.into_ms == 0);

    diagnostics::Gap gap{};
    CHECK(diagnostics::read(record, gap));
    CHECK(gap.dropped == 3);
    CHECK(gap.span_ms == 1100);
    CHECK(gap.total == 3);
    CHECK(gap.capacity == diagnostics::Recorder::kMaxRecords);
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: the gap lands before the first record that came after it") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++) recorder.record(fix(1), at_ms(i));
    recorder.record(fix(2), at_ms(2000));

    diagnostics::Record record{};
    for (int i = 0; i < 4; i++) CHECK(took(recorder, record));
    CHECK(recorder.record(fix(5), at_ms(4000)));

    while (took(recorder, record) && record.type == diagnostics::Type::Gnss) {
        diagnostics::Gnss out{};
        CHECK(diagnostics::read(record, out));
        CHECK(out.satellites == 1);
    }
    CHECK(record.type == diagnostics::Type::Gap);
    CHECK(took(recorder, record));
    diagnostics::Gnss out{};
    CHECK(diagnostics::read(record, out));
    CHECK(out.satellites == 5);
}

TEST_CASE("diag recorder: a gap costs the slot that freed it, so a ring still full drops again") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords; i++) recorder.record(fix(1), at_ms(i));
    CHECK_FALSE(recorder.record(fix(2), at_ms(2000)));

    diagnostics::Record record{};
    CHECK(took(recorder, record));
    CHECK_FALSE(recorder.record(fix(3), at_ms(2500)));
    CHECK(recorder.dropped() == 2);

    int gaps = 0;
    uint32_t counted = 0;
    while (took(recorder, record)) {
        if (record.type != diagnostics::Type::Gap) continue;
        gaps++;
        diagnostics::Gap gap{};
        CHECK(diagnostics::read(record, gap));
        counted += gap.dropped;
    }
    CHECK(gaps == 2);
    CHECK(counted == recorder.dropped());
}

// The writer takes the record only once flash has it: a refused write must lose nothing.
TEST_CASE("diag recorder: a record peeked and not committed is still in the ring") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    CHECK(recorder.record(fix(7), at_ms(0)));
    CHECK(recorder.record(fix(8), at_ms(1000)));

    diagnostics::Record record{};
    CHECK(recorder.peek(record));
    CHECK(recorder.peek(record));
    CHECK(recorder.queued() == 2);
    diagnostics::Gnss out{};
    CHECK(diagnostics::read(record, out));
    CHECK(out.satellites == 7);

    recorder.commit();
    CHECK(recorder.queued() == 1);
    CHECK(recorder.peek(record));
    CHECK(diagnostics::read(record, out));
    CHECK(out.satellites == 8);
}

TEST_CASE("diag recorder: everything the ring holds is drained once, gaps included") {
    diagnostics::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords + 10; i++)
        recorder.record(fix(1), at_ms(i));
    CHECK(recorder.dropped() == 10);
    CHECK(drain(recorder) == diagnostics::Recorder::kMaxRecords + 1);
    CHECK(drain(recorder) == 0);
}

TEST_CASE("diag recorder: a power run records the cell and the duty, and refuses the rest") {
    diagnostics::Recorder recorder;
    recorder.arm(diagnostics::Profile::PowerRun);
    CHECK(recorder.profile() == diagnostics::Profile::PowerRun);

    CHECK(recorder.record(diagnostics::Power{}, at_ms(0)));
    CHECK(recorder.record(diagnostics::Duty{}, at_ms(0)));
    CHECK_FALSE(recorder.record(fix(9), at_ms(1000)));
    CHECK_FALSE(recorder.record(diagnostics::Screen{}, at_ms(1000)));

    CHECK(recorder.queued() == 2);
    CHECK(recorder.written() == 2);
}

TEST_CASE("diag recorder: a flight run records what a power run records, and nothing more") {
    diagnostics::Recorder recorder;
    recorder.arm(diagnostics::Profile::FlightRun);
    CHECK(recorder.profile() == diagnostics::Profile::FlightRun);

    CHECK(recorder.record(diagnostics::Power{}, at_ms(0)));
    CHECK(recorder.record(diagnostics::Duty{}, at_ms(0)));
    CHECK_FALSE(recorder.record(diagnostics::Flight{}, at_ms(1000)));
    CHECK_FALSE(recorder.record(diagnostics::Screen{}, at_ms(1000)));
    CHECK(recorder.dropped() == 0);
    CHECK(recorder.queued() == 2);
}

TEST_CASE("diag recorder: a type the profile never wanted is not counted as a hole") {
    diagnostics::Recorder recorder;
    recorder.arm(diagnostics::Profile::PowerRun);
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords * 2; i++)
        recorder.record(fix(9), at_ms(0));

    CHECK(recorder.dropped() == 0);
    CHECK(recorder.queued() == 0);
}

TEST_CASE("diag recorder: a full capture records every subject the device decides") {
    diagnostics::Recorder recorder;
    recorder.arm();
    CHECK(recorder.profile() == diagnostics::Profile::Full);
    CHECK(recorder.record(fix(9), at_ms(0)));
    CHECK(recorder.record(diagnostics::Screen{}, at_ms(0)));
    CHECK(recorder.record(diagnostics::Duty{}, at_ms(0)));
    CHECK(recorder.queued() == 3);
}

TEST_CASE("diag recorder: a disarmed recorder refuses whatever profile it last held") {
    diagnostics::Recorder recorder;
    recorder.arm(diagnostics::Profile::PowerRun);
    recorder.disarm();
    CHECK_FALSE(recorder.record(diagnostics::Power{}, at_ms(0)));
}

// The figure the capture page divides its estimate by: one profile fills the ring in an hour,
// the other keeps a discharge run to cutoff whole.
TEST_CASE("diag recorder: a power run writes two records a pass and a capture fifteen a second") {
    CHECK(diagnostics::Recorder::records_per_hour(diagnostics::Profile::Full) == 15 * 3600);
    CHECK(diagnostics::Recorder::records_per_hour(diagnostics::Profile::PowerRun) == 240);
    CHECK(diagnostics::Recorder::records_per_hour(diagnostics::Profile::FlightRun) == 240);
}
