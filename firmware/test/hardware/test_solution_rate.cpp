// The product asks the solution rate it needs, and a part runs it or refuses it in plain sight.
#include "doctest/doctest.h"
#include "hardware/parts/ag3335/ag3335.h"
#include "hardware/parts/ag3335/model.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "products/common/solution_rate.h"
#include "products/skyblip_bravo/radio_protocols.h"
#include "products/skyblip_go/radio_protocols.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {

constexpr ports::SolutionRate kSecond = ports::SolutionRate::Second;
constexpr ports::SolutionRate kQuarters = ports::SolutionRate::Quarters;

}  // namespace

// A product that transmits ADS-L builds only on a receiver asked for, and able to solve, quarters.
static_assert(common::solution_rate_serves<parts::L76k>(go::kRadioProtocols, kQuarters));
static_assert(!common::solution_rate_serves<parts::L76k>(go::kRadioProtocols, kSecond));
static_assert(!common::solution_rate_serves<parts::Ag3335>(go::kRadioProtocols, kQuarters));
static_assert(!common::solution_rate_serves<parts::Ag3335>(go::kRadioProtocols, kSecond));
static_assert(common::solution_rate_serves<parts::Ag3335>(bravo::kRadioProtocols, kSecond));
static_assert(!common::solution_rate_serves<parts::Ag3335>(bravo::kRadioProtocols, kQuarters));

TEST_CASE("l76k: asked for one solution a second, the part never hears a faster $PCAS02") {
    models::L76k chip;
    parts::L76k gnss(kSecond, chip, chip);

    run(gnss, chip, 0, 10 * 60000);

    CHECK(chip.sub_second_rate_asked == 0);
    CHECK(chip.solution_period_ms == parts::L76k::kFactorySolutionPeriodMs);
    CHECK(gnss.configured());
    CHECK(gnss.cadence() == parts::L76k::Cadence::Second);
    CHECK(gnss.bring_up().quarter_verdict == ports::GnssVerdict::None);
    CHECK(gnss.solution().fix_valid);
}

TEST_CASE("l76k: asked for quarters, the part is asked $PCAS02,250 once") {
    models::L76k chip;
    parts::L76k gnss(kQuarters, chip, chip);

    run(gnss, chip, 0, kConfiguredMs);

    CHECK(chip.quarter_rate_asked == 1);
    CHECK(gnss.solving_quarters());
}

// A rate the AG3335 driver cannot run is a fault on the self-test and in the dump, not a 1 Hz unit.
TEST_CASE("ag3335: asked for quarters, the driver configures nothing and publishes no fix") {
    models::Ag3335 chip;
    parts::Ag3335 gnss(kQuarters, chip);

    for (uint32_t now_ms = 0; now_ms <= 30000; now_ms += 10) {
        chip.tick(now_ms);
        gnss.service(now_ms);
        CHECK_FALSE(gnss.poll());
    }

    CHECK(gnss.degraded());
    CHECK(gnss.health().config_stage == ports::GnssConfigStage::Degraded);
    CHECK(chip.heard().empty());
    CHECK_FALSE(gnss.solution().fix_valid);
}
