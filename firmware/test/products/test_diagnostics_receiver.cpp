// The receiver record: what the L76K driver sent the part and decided, as a capture keeps it.
#include <cstdio>
#include <string>
#include <vector>

#include "core/diagnostics/payload.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/diagnostics_taps.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

void receiver_seconds(Rig& rig, uint32_t& now_ms, uint32_t seconds) {
    for (uint32_t elapsed = 0; elapsed < seconds * 1000; elapsed += 10) {
        rig.platform.chips().gnss.tick(now_ms);
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        now_ms += 10;
    }
}

std::vector<ports::GnssEvent> receiver_records(const std::vector<diagnostics::Record>& records) {
    std::vector<ports::GnssEvent> events;
    for (const diagnostics::Record& record : records) {
        ports::GnssEvent event{};
        if (diagnostics::read(record, event)) events.push_back(event);
    }
    return events;
}

}  // namespace

TEST_CASE("diagnostics receiver: each command the part is sent is a record, with its uptime") {
    Rig rig;
    rig.platform.retune_gnss_port(true);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    arm(rig, now_ms);
    receiver_seconds(rig, now_ms, 15);
    stop(rig, now_ms);

    const std::vector<ports::GnssEvent> events = receiver_records(captured(rig));
    std::vector<ports::GnssCommand> commands;
    for (const ports::GnssEvent& event : events)
        if (event.kind == ports::GnssEventKind::Command) commands.push_back(event.command);
    REQUIRE(commands.size() >= 9);
    CHECK(commands[0] == ports::GnssCommand::Wake);
    CHECK(commands[6] == ports::GnssCommand::Baud115200);
    CHECK(commands[7] == ports::GnssCommand::Rate4Hz);
    CHECK(events.front().at_ms < events.back().at_ms);
    CHECK(events.back().at_ms < now_ms);
}

TEST_CASE("diagnostics receiver: the 4 Hz verdict carries the cadence and stamps the part kept") {
    Rig rig;
    rig.platform.retune_gnss_port(true);
    rig.platform.chips().gnss.fastest_interval_ms = parts::L76k::kHalfSolutionPeriodMs;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    arm(rig, now_ms);
    receiver_seconds(rig, now_ms, 15);
    stop(rig, now_ms);

    const ports::GnssEvent* refusal = nullptr;
    const std::vector<ports::GnssEvent> events = receiver_records(captured(rig));
    for (const ports::GnssEvent& event : events)
        if (event.kind == ports::GnssEventKind::Verdict &&
            event.interval_ms == parts::L76k::kSolutionPeriodMs)
            refusal = &event;
    REQUIRE(refusal != nullptr);
    CHECK(refusal->verdict == ports::GnssVerdict::OffCadence);
    CHECK(refusal->interval_seen_ms == parts::L76k::kFactorySolutionPeriodMs);
    CHECK(refusal->stamp_ms == 0);
    CHECK(refusal->steps == 0);
    CHECK(refusal->baud == parts::L76k::kTargetBaudRate);
    CHECK(refusal->stage == ports::GnssConfigStage::Verifying);
}

TEST_CASE("diagnostics receiver: a record reads back as the event it was written from") {
    ports::GnssEvent event{};
    event.kind = ports::GnssEventKind::Verdict;
    event.verdict = ports::GnssVerdict::Silent;
    event.stage = ports::GnssConfigStage::Probing;
    event.at_ms = 1234567;
    event.baud = 38400;
    event.interval_ms = 250;
    event.interval_seen_ms = 500;
    event.stamp_ms = 750;
    event.steps = 3;
    event.heard = 0;

    ports::GnssEvent read{};
    REQUIRE(diagnostics::read(diagnostics::record_of(event, diagnostics::Instant{}), read));
    CHECK(read.kind == event.kind);
    CHECK(read.verdict == event.verdict);
    CHECK(read.stage == event.stage);
    CHECK(read.at_ms == event.at_ms);
    CHECK(read.baud == event.baud);
    CHECK(read.interval_seen_ms == event.interval_seen_ms);
    CHECK(read.stamp_ms == event.stamp_ms);
    CHECK(read.steps == event.steps);

    event.stamp_ms = -1;
    event.interval_seen_ms = 0;
    REQUIRE(diagnostics::read(diagnostics::record_of(event, diagnostics::Instant{}), read));
    CHECK(read.stamp_ms == -1);
    CHECK(read.interval_seen_ms == 0);
}

// scripts/test_blip.py and server/test/javascript/test_diagnostics.mjs decode these same bytes.
TEST_CASE("diagnostics receiver: a refused 4 Hz encodes to the bytes both decoders are pinned to") {
    ports::GnssEvent event{};
    event.kind = ports::GnssEventKind::Verdict;
    event.verdict = ports::GnssVerdict::OffCadence;
    event.stage = ports::GnssConfigStage::Verifying;
    event.at_ms = 7270;
    event.baud = 115200;
    event.interval_ms = 250;
    event.interval_seen_ms = 1000;
    event.stamp_ms = 0;
    event.heard = 4;
    diagnostics::Instant at{};
    at.at_s = 1785628807;
    at.into_ms = 270;
    at.phase_valid = true;
    at.utc_dated = true;

    uint8_t raw[diagnostics::kRecordBytes];
    diagnostics::encode_record(diagnostics::record_of(event, at), raw);
    char hex[2 * diagnostics::kRecordBytes + 1];
    for (uint32_t i = 0; i < diagnostics::kRecordBytes; i++)
        std::snprintf(hex + 2 * i, 3, "%02x", raw[i]);
    CHECK(std::string(hex) == "163b0e0187886e6a05000405661c0000fa00e80300000004");
}
