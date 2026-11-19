// A receiver that falls silent is listened for until it answers, and reset when it will not.
#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {

constexpr int kResetPin = 37;

}  // namespace

// The pinned case: Go 000001's part went silent in flight and the driver ended degraded for good.
TEST_CASE("l76k: a part that falls silent while ready is probed, and configured when it answers") {
    models::L76k chip;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_half_seconds());

    chip.hung = true;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 30000);
    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
    CHECK(chip.baud_changes >= 2 * parts::L76k::kBaudCandidateCount);
    CHECK_FALSE(gnss.solution().fix_valid);

    chip.hung = false;
    run(gnss, chip, kConfiguredMs + 30010, kConfiguredMs + 60000);
    CHECK(gnss.configured());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.cadence() == parts::L76k::Cadence::Half);
    CHECK(chip.sub_second_rate_asked == 2);
}

// Quectel's own advice for a module in an abnormal state, Hardware Design V1.0 4.2.1.
TEST_CASE("l76k: a part silent for three probe rounds has its RESET_N held low, and comes back") {
    models::L76k chip;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip, &chip, kResetPin);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_half_seconds());

    chip.hung = true;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 60000);

    CHECK(chip.hardware_resets == 1);
    CHECK(gnss.configured());
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.baud_rate() == parts::L76k::kTargetBaudRate);
    CHECK(gnss.cadence() == parts::L76k::Cadence::Half);
}

// Quectel lists 500 ms, so a part it silences is a broken part: it is reset again and again,
// loudly.
TEST_CASE("l76k: a part that hangs on $PCAS02,500 is reset each time, never settled at 1 Hz") {
    models::L76k chip;
    chip.hangs_on_fast_rate = true;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip, &chip, kResetPin);

    run(gnss, chip, 0, 120000);

    CHECK(chip.sub_second_rate_asked >= 2);
    CHECK(chip.hardware_resets >= 2);
    CHECK_FALSE(gnss.holds_rate());
}

TEST_CASE("l76k: a silent part on a board with no reset line is probed for good, never reset") {
    models::L76k chip;
    chip.reset_pin = kResetPin;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    chip.hung = true;

    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 120000);

    CHECK(chip.hardware_resets == 0);
    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
    CHECK(chip.baud_changes >= 10 * parts::L76k::kBaudCandidateCount);
}

TEST_CASE("l76k: a degraded part that falls silent is probed too") {
    models::L76k chip;
    chip.accepts_commands = false;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    uint32_t now_ms = 0;
    for (; now_ms < 30000 && !gnss.degraded(); now_ms += 10) run(gnss, chip, now_ms, now_ms);
    REQUIRE(gnss.degraded());

    chip.hung = true;
    run(gnss, chip, now_ms, now_ms + parts::L76k::kSilenceMs + 500);

    CHECK(gnss.config_stage() == parts::L76k::ConfigStage::Probing);
}
