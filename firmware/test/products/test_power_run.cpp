// A power run end to end: the span it quotes while running, and the pair it closes on at park.
#include <cstddef>
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/diagnostics/profile.h"
#include "core/power/cutoff.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint16_t kBelowCutoffMv = power::kFlatMv - 100;

void taxi(Rig& rig, uint32_t& now_ms, uint32_t seconds) { rig.seconds(now_ms, seconds, 0, 300); }

void arm_power_run(Rig& rig, uint32_t& now_ms) {
    rig.product.diagnostics().arm(diagnostics::Profile::PowerRun);
    rig.run(now_ms, now_ms + 100);
    now_ms += 100;
    REQUIRE(rig.product.capture().capturing());
}

void run_until_parked(Rig& rig, uint32_t& now_ms) {
    const uint32_t deadline = now_ms + 20000;
    while (rig.product.capture().capturing() && now_ms < deadline) {
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        now_ms += 50;
    }
    REQUIRE_FALSE(rig.product.capture().capturing());
}

std::vector<diagnostics::Record> tail(const std::vector<diagnostics::Record>& records,
                                      size_t count) {
    REQUIRE(records.size() >= count);
    return {records.end() - static_cast<std::ptrdiff_t>(count), records.end()};
}

bool same_instant(const diagnostics::Record& first, const diagnostics::Record& second) {
    return first.at_s == second.at_s && first.into_ms == second.into_ms;
}

uint32_t quoted_keeps_s(Rig& rig) { return rig.state().capture.keeps_s; }

uint32_t nominal_keeps_s(Rig& rig) {
    return rig.product.capture().keeps_s(diagnostics::Profile::PowerRun);
}

}  // namespace

// The Boot and Config a session opens with are not a rate: counted as one, they cut KEEPS sixfold.
TEST_CASE("power run: before its first pair lands, the span quoted is the nominal one") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm_power_run(rig, now_ms);
    rig.run(now_ms, now_ms + 5000);
    now_ms += 5000;

    CHECK(rig.product.capture().records_written() == 2);
    CHECK(quoted_keeps_s(rig) == nominal_keeps_s(rig));
}

TEST_CASE("power run: once pairs land, the span quoted is the one they are landing at") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm_power_run(rig, now_ms);

    for (uint32_t elapsed_s = 7; elapsed_s <= 91; elapsed_s += 7) {
        rig.run(now_ms, now_ms + 7000);
        now_ms += 7000;
        // the first pair reaches the flash within a second of 30 s after the session opened
        CHECK(quoted_keeps_s(rig) >= nominal_keeps_s(rig) * 29 / 30);
        CHECK(quoted_keeps_s(rig) <= nominal_keeps_s(rig) * 31 / 30);
    }
}

// Cutoff stops the service loop on the pass the level turns: the 30 s gate never opens again.
TEST_CASE("park: a power run taken down by its cell ends on a cutoff power record, then duty") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm_power_run(rig, now_ms);
    rig.run(now_ms, now_ms + 40000);
    now_ms += 40000;
    rig.platform.battery().divider_mv = kBelowCutoffMv;
    run_until_parked(rig, now_ms);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);

    const std::vector<diagnostics::Record> last = tail(captured(rig), 3);
    diagnostics::Power power{};
    REQUIRE(diagnostics::read(last[0], power));
    CHECK(power.power_level == power::PowerLevel::Flat);
    CHECK(last[1].type == diagnostics::Type::Duty);
    CHECK(last[2].type == diagnostics::Type::End);
    CHECK(same_instant(last[0], last[1]));
}

TEST_CASE("park: a power run the pilot switches off closes on power, then duty, all the same") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    arm_power_run(rig, now_ms);
    rig.run(now_ms, now_ms + 40000);
    now_ms += 40000;
    rig.product.shutdown().request(power::ShutdownReason::LongPress, now_ms);
    run_until_parked(rig, now_ms);

    const std::vector<diagnostics::Record> records = captured(rig);
    const std::vector<diagnostics::Record> last = tail(records, 3);
    diagnostics::Power power{};
    REQUIRE(diagnostics::read(last[0], power));
    CHECK(power.power_level == power::PowerLevel::Normal);
    CHECK(last[1].type == diagnostics::Type::Duty);
    CHECK(last[2].type == diagnostics::Type::End);
    CHECK(same_instant(last[0], last[1]));
    // Boot, Config, the pair the 30 s gate wrote, the pair the park wrote, End
    CHECK(records.size() == 7);
}

TEST_CASE("park: a full capture closes on the same pair, off its own cadence") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 3);
    rig.product.diagnostics().arm(diagnostics::Profile::Full);
    taxi(rig, now_ms, 4);
    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, now_ms);
    run_until_parked(rig, now_ms);

    const std::vector<diagnostics::Record> last = tail(captured(rig), 3);
    CHECK(last[0].type == diagnostics::Type::Power);
    CHECK(last[1].type == diagnostics::Type::Duty);
    CHECK(last[2].type == diagnostics::Type::End);
    CHECK(same_instant(last[0], last[1]));
}
