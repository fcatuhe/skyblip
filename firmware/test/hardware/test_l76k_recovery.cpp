// A receiver that falls silent is listened for until it answers, and reset when it will not.
#include <vector>

#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {

constexpr int kResetPin = 37;

std::vector<ports::GnssEvent> journal(parts::L76k& driver, models::L76k& chip, uint32_t from_ms,
                                      uint32_t to_ms) {
    std::vector<ports::GnssEvent> events;
    for (uint32_t now_ms = from_ms; now_ms <= to_ms; now_ms += 10) {
        chip.tick(now_ms);
        driver.service(now_ms);
        driver.poll();
        ports::GnssEvent event;
        while (driver.take_event(event)) events.push_back(event);
    }
    return events;
}

}  // namespace

// The pinned case: Go 000001's part went silent in flight and the driver ended degraded for good.
TEST_CASE("l76k: a part that falls silent while ready is probed, and configured when it answers") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_quarters());

    chip.hung = true;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 30000);
    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
    CHECK(gnss.bring_up().probes >= 2);
    CHECK_FALSE(gnss.solution().fix_valid);

    chip.hung = false;
    run(gnss, chip, kConfiguredMs + 30010, kConfiguredMs + 60000);
    CHECK(gnss.configured());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.cadence() == parts::L76k::Cadence::Half);
    CHECK(chip.quarter_rate_asked == 1);
}

// Quectel's own advice for a module in an abnormal state, Hardware Design V1.0 4.2.1.
TEST_CASE("l76k: a part silent for three probe rounds has its RESET_N held low, and comes back") {
    models::L76k chip;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(chip, chip, &chip, kResetPin);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_quarters());

    chip.hung = true;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 60000);

    CHECK(chip.hardware_resets == 1);
    CHECK(gnss.bring_up().resets == 1);
    CHECK(gnss.configured());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.baud_rate() == parts::L76k::kTargetBaudRate);
    CHECK(gnss.cadence() == parts::L76k::Cadence::Half);
}

// What the device test could not tell apart: a part that refuses 250 ms and one that dies of it.
TEST_CASE("l76k: a part that hangs on $PCAS02,250 is reset, says so, and settles at 2 Hz") {
    models::L76k chip;
    chip.hangs_on_quarter_rate = true;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(chip, chip, &chip, kResetPin);

    run(gnss, chip, 0, 120000);

    CHECK(chip.quarter_rate_asked == 1);
    CHECK(chip.hardware_resets == 1);
    CHECK(gnss.bring_up().quarter_verdict == ports::GnssVerdict::Silent);
    CHECK(gnss.cadence() == parts::L76k::Cadence::Half);
    CHECK(gnss.solution().fix_valid);
}

TEST_CASE("l76k: a silent part on a board with no reset line is probed for good, never reset") {
    models::L76k chip;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    chip.hung = true;

    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 120000);

    CHECK(chip.hardware_resets == 0);
    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
    CHECK(gnss.bring_up().probes >= 10);
}

TEST_CASE("l76k: a degraded part that falls silent is probed too") {
    models::L76k chip;
    chip.accepts_commands = false;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, 30000);
    REQUIRE(gnss.degraded());

    chip.hung = true;
    run(gnss, chip, 30010, 40000);

    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
}

// The journal is what a capture records: every command in the order the part heard it.
TEST_CASE("l76k: every command, stage and verdict of a bring-up is journalled in order") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);

    const std::vector<ports::GnssEvent> events = journal(gnss, chip, 0, kConfiguredMs);

    std::vector<ports::GnssCommand> commands;
    std::vector<ports::GnssEvent> verdicts;
    for (const ports::GnssEvent& event : events) {
        if (event.kind == ports::GnssEventKind::Command) commands.push_back(event.command);
        if (event.kind == ports::GnssEventKind::Verdict) verdicts.push_back(event);
    }
    const std::vector<ports::GnssCommand> sent = {
        ports::GnssCommand::Wake,           ports::GnssCommand::Identify,
        ports::GnssCommand::Constellations, ports::GnssCommand::SentenceSet,
        ports::GnssCommand::DynamicModel,   ports::GnssCommand::Rate1Hz,
        ports::GnssCommand::Baud115200,     ports::GnssCommand::Rate4Hz,
        ports::GnssCommand::Gsa4,
    };
    CHECK(commands == sent);
    CHECK(gnss.bring_up().commands == sent.size());
    CHECK(gnss.bring_up().command == ports::GnssCommand::Gsa4);
    REQUIRE(verdicts.size() == 2);
    CHECK(verdicts[1].verdict == ports::GnssVerdict::Passed);
    CHECK(verdicts[1].interval_ms == parts::L76k::kSolutionPeriodMs);
    CHECK(verdicts[1].interval_seen_ms == parts::L76k::kSolutionPeriodMs);
    CHECK(verdicts[1].steps >= parts::L76k::kMinQuarterSteps);
    CHECK(verdicts[1].stamp_ms % 250 == 0);
    CHECK(events.back().stage == parts::L76k::ConfigStage::Ready);
}

TEST_CASE("l76k: a refused 4 Hz is journalled with the cadence and the stamps the part kept") {
    models::L76k chip;
    chip.fastest_interval_ms = parts::L76k::kHalfSolutionPeriodMs;
    parts::L76k gnss(chip, chip);

    const std::vector<ports::GnssEvent> events = journal(gnss, chip, 0, kConfiguredMs);

    const ports::GnssEvent* refusal = nullptr;
    for (const ports::GnssEvent& event : events)
        if (event.kind == ports::GnssEventKind::Verdict && event.interval_ms == 250)
            refusal = &event;
    REQUIRE(refusal != nullptr);
    CHECK(refusal->verdict == ports::GnssVerdict::OffCadence);
    CHECK(refusal->interval_seen_ms == parts::L76k::kFactorySolutionPeriodMs);
    CHECK(refusal->stamp_ms == 0);
    CHECK(refusal->steps == 0);
    CHECK(refusal->heard >= parts::L76k::kMinVerifySentences);
}
