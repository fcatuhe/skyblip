// A power run end to end: the span it quotes while running, and the pair it closes on at park.
#include <cstddef>
#include <vector>

#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "core/power/cutoff.h"
#include "core/power/shutdown.h"
#include "doctest/doctest.h"
#include "test/support/diag_corpus.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

constexpr uint16_t kBelowCutoffMv = power::kCutoffMv - 100;

void taxi(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 0, 300); }

void arm_power_run(Rig& rig, uint32_t& t) {
    rig.product.diag().arm(diag::Profile::PowerRun);
    rig.run(t, t + 100);
    t += 100;
    REQUIRE(rig.product.capture().capturing());
}

void run_until_parked(Rig& rig, uint32_t& t) {
    const uint32_t deadline = t + 20000;
    while (rig.product.capture().capturing() && t < deadline) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        t += 50;
    }
    REQUIRE_FALSE(rig.product.capture().capturing());
}

std::vector<diag::Record> tail(const std::vector<diag::Record>& records, size_t n) {
    REQUIRE(records.size() >= n);
    return {records.end() - static_cast<std::ptrdiff_t>(n), records.end()};
}

bool same_instant(const diag::Record& a, const diag::Record& b) {
    return a.at_s == b.at_s && a.into_ms == b.into_ms;
}

uint32_t quoted_keeps_s(Rig& rig) { return rig.state().capture.keeps_s; }

uint32_t nominal_keeps_s(Rig& rig) {
    return rig.product.capture().keeps_s(diag::Profile::PowerRun);
}

}  // namespace

// The Boot and Config a session opens with are not a rate: counted as one, they cut KEEPS sixfold.
TEST_CASE("power run: before its first pair lands, the span quoted is the nominal one") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_power_run(rig, t);
    rig.run(t, t + 5000);
    t += 5000;

    CHECK(rig.product.capture().records_written() == 2);
    CHECK(quoted_keeps_s(rig) == nominal_keeps_s(rig));
}

TEST_CASE("power run: once pairs land, the span quoted is the one they are landing at") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_power_run(rig, t);

    for (uint32_t elapsed_s = 7; elapsed_s <= 91; elapsed_s += 7) {
        rig.run(t, t + 7000);
        t += 7000;
        // the first pair reaches the flash within a second of 30 s after the session opened
        CHECK(quoted_keeps_s(rig) >= nominal_keeps_s(rig) * 29 / 30);
        CHECK(quoted_keeps_s(rig) <= nominal_keeps_s(rig) * 31 / 30);
    }
}

// Cutoff stops the service loop on the pass the level turns: the 30 s gate never opens again.
TEST_CASE("park: a power run taken down by its cell ends on a cutoff power record, then duty") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_power_run(rig, t);
    rig.run(t, t + 40000);
    t += 40000;
    rig.platform.battery().millivolts = kBelowCutoffMv;
    run_until_parked(rig, t);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::LowBattery);

    const std::vector<diag::Record> last = tail(captured(rig), 3);
    diag::Power power{};
    REQUIRE(diag::read(last[0], power));
    CHECK(power.level == power::PowerLevel::Cutoff);
    CHECK(last[1].type == diag::Type::Duty);
    CHECK(last[2].type == diag::Type::End);
    CHECK(same_instant(last[0], last[1]));
}

TEST_CASE("park: a power run the pilot switches off closes on power, then duty, all the same") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_power_run(rig, t);
    rig.run(t, t + 40000);
    t += 40000;
    rig.product.shutdown().request(power::ShutdownReason::LongPress, t);
    run_until_parked(rig, t);

    const std::vector<diag::Record> records = captured(rig);
    const std::vector<diag::Record> last = tail(records, 3);
    diag::Power power{};
    REQUIRE(diag::read(last[0], power));
    CHECK(power.level == power::PowerLevel::Normal);
    CHECK(last[1].type == diag::Type::Duty);
    CHECK(last[2].type == diag::Type::End);
    CHECK(same_instant(last[0], last[1]));
    // Boot, Config, the pair the 30 s gate wrote, the pair the park wrote, End
    CHECK(records.size() == 7);
}

TEST_CASE("park: a full capture closes on the same pair, off its own cadence") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    rig.product.diag().arm(diag::Profile::Full);
    taxi(rig, t, 4);
    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    run_until_parked(rig, t);

    const std::vector<diag::Record> last = tail(captured(rig), 3);
    CHECK(last[0].type == diag::Type::Power);
    CHECK(last[1].type == diag::Type::Duty);
    CHECK(last[2].type == diag::Type::End);
    CHECK(same_instant(last[0], last[1]));
}
