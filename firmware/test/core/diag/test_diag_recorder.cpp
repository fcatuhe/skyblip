// The RAM ring a capture fills: disarmed at boot, drained by the writer, honest about every drop.
#include "core/diag/recorder.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kBaseUtc = 1789000000;

diag::Instant at_ms(uint32_t millis) {
    diag::Instant at{};
    at.at_s = kBaseUtc + millis / 1000;
    at.into_ms = static_cast<uint16_t>(millis % 1000);
    at.phase_valid = true;
    at.utc_dated = true;
    return at;
}

diag::Gnss fix(uint8_t sats) {
    diag::Gnss value{};
    value.sats = sats;
    value.fix_valid = true;
    return value;
}

bool took(diag::Recorder& recorder, diag::Record& out) {
    if (!recorder.peek(out)) return false;
    recorder.commit();
    return true;
}

int drain(diag::Recorder& recorder) {
    diag::Record record{};
    int taken = 0;
    while (took(recorder, record)) taken++;
    return taken;
}

}  // namespace

TEST_CASE("diag recorder: a boot comes up disarmed and records nothing") {
    diag::Recorder recorder{};
    CHECK_FALSE(recorder.armed());
    CHECK_FALSE(recorder.record(fix(7), at_ms(0)));
    CHECK(recorder.queued() == 0);
    CHECK(recorder.written() == 0);
    CHECK(recorder.dropped() == 0);
    diag::Record record{};
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: disarming stops the capture and leaves the tail drainable") {
    diag::Recorder recorder{};
    recorder.arm();
    CHECK(recorder.record(fix(7), at_ms(0)));
    recorder.disarm();
    CHECK_FALSE(recorder.record(fix(8), at_ms(1000)));
    CHECK(recorder.queued() == 1);
    diag::Record record{};
    CHECK(took(recorder, record));
    diag::Gnss out{};
    CHECK(diag::read(record, out));
    CHECK(out.sats == 7);
}

TEST_CASE("diag recorder: arming again after a disarm starts a clean corpus") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity + 4; i++) recorder.record(fix(1), at_ms(i));
    CHECK(recorder.dropped() == 4);
    recorder.disarm();
    recorder.arm();
    CHECK(recorder.queued() == 0);
    CHECK(recorder.dropped() == 0);
    CHECK(recorder.written() == 0);
}

TEST_CASE("diag recorder: records come back in the order they were taken") {
    diag::Recorder recorder{};
    recorder.arm();
    for (uint8_t i = 0; i < 5; i++) CHECK(recorder.record(fix(i), at_ms(i)));

    for (uint8_t i = 0; i < 5; i++) {
        diag::Record record{};
        CHECK(took(recorder, record));
        diag::Gnss out{};
        CHECK(diag::read(record, out));
        CHECK(out.sats == i);
    }
    diag::Record record{};
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: a burst carries its own instant off the tape") {
    diag::Recorder recorder{};
    recorder.arm();
    radio::Entry entry{};
    entry.event = radio::Event::Transmitted;
    entry.at_s = kBaseUtc;
    entry.into_ms = 462;
    entry.phase_valid = true;
    entry.utc = true;
    CHECK(recorder.record(entry));

    diag::Record record{};
    CHECK(took(recorder, record));
    CHECK(record.type == diag::Type::Burst);
    CHECK(record.at_s == kBaseUtc);
    CHECK(record.into_ms == 462);
}

TEST_CASE("diag recorder: a full ring refuses the new record rather than the history") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity; i++) CHECK(recorder.record(fix(3), at_ms(i)));
    CHECK_FALSE(recorder.record(fix(9), at_ms(1000)));
    CHECK(recorder.dropped() == 1);
    CHECK(recorder.queued() == diag::Recorder::kCapacity);

    diag::Record record{};
    CHECK(took(recorder, record));
    diag::Gnss out{};
    CHECK(diag::read(record, out));
    CHECK(out.sats == 3);
}

TEST_CASE("diag recorder: the hole is a gap record where the hole is") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity; i++) recorder.record(fix(1), at_ms(i));
    recorder.record(fix(2), at_ms(2000));
    recorder.record(fix(2), at_ms(2400));
    recorder.record(fix(2), at_ms(3100));
    CHECK(recorder.dropped() == 3);

    diag::Record record{};
    for (int i = 0; i < diag::Recorder::kCapacity; i++) {
        CHECK(took(recorder, record));
        CHECK(record.type == diag::Type::Gnss);
    }
    CHECK(took(recorder, record));
    CHECK(record.type == diag::Type::Gap);
    CHECK(record.at_s == kBaseUtc + 2);
    CHECK(record.into_ms == 0);

    diag::Gap gap{};
    CHECK(diag::read(record, gap));
    CHECK(gap.dropped == 3);
    CHECK(gap.span_ms == 1100);
    CHECK(gap.total == 3);
    CHECK(gap.capacity == diag::Recorder::kCapacity);
    CHECK_FALSE(took(recorder, record));
}

TEST_CASE("diag recorder: the gap lands before the first record that came after it") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity; i++) recorder.record(fix(1), at_ms(i));
    recorder.record(fix(2), at_ms(2000));

    diag::Record record{};
    for (int i = 0; i < 4; i++) CHECK(took(recorder, record));
    CHECK(recorder.record(fix(5), at_ms(4000)));

    while (took(recorder, record) && record.type == diag::Type::Gnss) {
        diag::Gnss out{};
        CHECK(diag::read(record, out));
        CHECK(out.sats == 1);
    }
    CHECK(record.type == diag::Type::Gap);
    CHECK(took(recorder, record));
    diag::Gnss out{};
    CHECK(diag::read(record, out));
    CHECK(out.sats == 5);
}

TEST_CASE("diag recorder: a gap costs the slot that freed it, so a ring still full drops again") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity; i++) recorder.record(fix(1), at_ms(i));
    CHECK_FALSE(recorder.record(fix(2), at_ms(2000)));

    diag::Record record{};
    CHECK(took(recorder, record));
    CHECK_FALSE(recorder.record(fix(3), at_ms(2500)));
    CHECK(recorder.dropped() == 2);

    int gaps = 0;
    uint32_t counted = 0;
    while (took(recorder, record)) {
        if (record.type != diag::Type::Gap) continue;
        gaps++;
        diag::Gap gap{};
        CHECK(diag::read(record, gap));
        counted += gap.dropped;
    }
    CHECK(gaps == 2);
    CHECK(counted == recorder.dropped());
}

// The writer takes the record only once flash has it: a refused write must lose nothing.
TEST_CASE("diag recorder: a record peeked and not committed is still in the ring") {
    diag::Recorder recorder{};
    recorder.arm();
    CHECK(recorder.record(fix(7), at_ms(0)));
    CHECK(recorder.record(fix(8), at_ms(1000)));

    diag::Record record{};
    CHECK(recorder.peek(record));
    CHECK(recorder.peek(record));
    CHECK(recorder.queued() == 2);
    diag::Gnss out{};
    CHECK(diag::read(record, out));
    CHECK(out.sats == 7);

    recorder.commit();
    CHECK(recorder.queued() == 1);
    CHECK(recorder.peek(record));
    CHECK(diag::read(record, out));
    CHECK(out.sats == 8);
}

TEST_CASE("diag recorder: everything the ring holds is drained once, gaps included") {
    diag::Recorder recorder{};
    recorder.arm();
    for (int i = 0; i < diag::Recorder::kCapacity + 10; i++) recorder.record(fix(1), at_ms(i));
    CHECK(recorder.dropped() == 10);
    CHECK(drain(recorder) == diag::Recorder::kCapacity + 1);
    CHECK(drain(recorder) == 0);
}

TEST_CASE("diag recorder: a power run records the cell and the duty, and refuses the rest") {
    diag::Recorder recorder;
    recorder.arm(diag::Profile::PowerRun);
    CHECK(recorder.profile() == diag::Profile::PowerRun);

    CHECK(recorder.record(diag::Power{}, at_ms(0)));
    CHECK(recorder.record(diag::Duty{}, at_ms(0)));
    CHECK_FALSE(recorder.record(fix(9), at_ms(1000)));
    CHECK_FALSE(recorder.record(diag::Screen{}, at_ms(1000)));

    CHECK(recorder.queued() == 2);
    CHECK(recorder.written() == 2);
}

TEST_CASE("diag recorder: a type the profile never wanted is not counted as a hole") {
    diag::Recorder recorder;
    recorder.arm(diag::Profile::PowerRun);
    for (int i = 0; i < diag::Recorder::kCapacity * 2; i++) recorder.record(fix(9), at_ms(0));

    CHECK(recorder.dropped() == 0);
    CHECK(recorder.queued() == 0);
}

TEST_CASE("diag recorder: a full capture records every subject the device decides") {
    diag::Recorder recorder;
    recorder.arm();
    CHECK(recorder.profile() == diag::Profile::Full);
    CHECK(recorder.record(fix(9), at_ms(0)));
    CHECK(recorder.record(diag::Screen{}, at_ms(0)));
    CHECK(recorder.record(diag::Duty{}, at_ms(0)));
    CHECK(recorder.queued() == 3);
}

TEST_CASE("diag recorder: a disarmed recorder refuses whatever profile it last held") {
    diag::Recorder recorder;
    recorder.arm(diag::Profile::PowerRun);
    recorder.disarm();
    CHECK_FALSE(recorder.record(diag::Power{}, at_ms(0)));
}

// The figure the capture page divides its estimate by: one profile fills the ring in an hour,
// the other keeps a discharge run to cutoff whole.
TEST_CASE("diag recorder: a power run writes two records a pass and a capture eleven a second") {
    CHECK(diag::Recorder::records_per_hour(diag::Profile::Full) == 11 * 3600);
    CHECK(diag::Recorder::records_per_hour(diag::Profile::PowerRun) == 240);
}
