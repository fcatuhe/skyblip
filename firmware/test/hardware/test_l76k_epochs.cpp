// What the driver publishes as one solution, and how it knows the part solves on .000 and .500.
#include <cstdio>
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {

// The wire as a case scripts it: whatever was queued is what the next drain reads.
class ScriptedWire : public io::Uart {
   public:
    size_t write(const uint8_t*, size_t bytes) override { return bytes; }
    size_t read(uint8_t* out, size_t capacity) override {
        const size_t count = queued_.size() < capacity ? queued_.size() : capacity;
        for (size_t i = 0; i < count; i++) out[i] = static_cast<uint8_t>(queued_[i]);
        queued_.erase(0, count);
        return count;
    }
    size_t available() override { return queued_.size(); }
    void say(const std::string& line) { queued_ += line; }

   private:
    std::string queued_;
};

std::string checksummed(const std::string& body) {
    uint8_t checksum = 0;
    for (const char character : body) checksum ^= static_cast<uint8_t>(character);
    char tail[6];
    std::snprintf(tail, sizeof(tail), "*%02X\r\n", checksum);
    return "$" + body + tail;
}

std::string gga(const char* stamp, int altitude_m) {
    return checksummed(std::string("GPGGA,") + stamp + ",4807.038,N,01131.000,E,1,08,0.9," +
                       std::to_string(altitude_m) + ".0,M,47.0,M,,");
}

std::string rmc(const char* stamp, const char* latitude = "4807.038") {
    return checksummed(std::string("GPRMC,") + stamp + ",A," + latitude +
                       ",N,01131.000,E,022.4,084.4,230325,,,A");
}

constexpr int32_t kGeoidMm = 47000;

}  // namespace

// The pinned case: the validity edge published an RMC beside the next solution's GGA height.
TEST_CASE("l76k: an RMC whose GGA never came, then the next GGA, is not a fix") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(rmc("123519.000"));
    gnss.poll(1000);
    wire.say(gga("123519.500", 1300));
    gnss.poll(1010);
    CHECK_FALSE(gnss.solution().fix_valid);

    wire.say(rmc("123519.500", "4807.050"));
    REQUIRE(gnss.poll(1020));
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().utc_ms == 500);
    CHECK(gnss.solution().altitude_hae_mm == 1300000 + kGeoidMm);
}

// The pinned case: one drain held a whole .000 pair and the next GGA, and mixed the two.
TEST_CASE("l76k: a drain holding a whole pair and the next GGA publishes the pair as it was") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000") + gga("123520.500", 1005));
    REQUIRE(gnss.poll(1000));
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().utc_ms == 0);
    CHECK(gnss.solution().altitude_hae_mm == 1000000 + kGeoidMm);
}

// A receiver that goes quiet is still withdrawn without a pair: invalidation needs none.
TEST_CASE("l76k: a fix is withdrawn without a pair, and never promoted without one") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000"));
    REQUIRE(gnss.poll(1000));
    REQUIRE(gnss.solution().fix_valid);

    CHECK(gnss.poll(1000 + gnss::kSentenceMaxAgeMs + 10));
    CHECK_FALSE(gnss.solution().fix_valid);
    CHECK(gnss.solution().altitude_hae_mm == 1000000 + kGeoidMm);
}

// G.1.1 counts quarters: a part at 5 Hz passes a count of solutions, never a count of half steps.
TEST_CASE("l76k: a part solving every 200 ms is not taken for one solving on .000 and .500") {
    models::L76k chip;
    chip.fast_rate_runs_at_ms = 200;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);

    run(gnss, chip, 0, 60000);

    CHECK_FALSE(gnss.solving_half_seconds());
    CHECK_FALSE(gnss.holds_rate());
    CHECK(gnss.recoveries() >= 1);
}

// A part that took 2 Hz and then fell back, after a restart or on its own, is asked again.
TEST_CASE("l76k: a part that drifts off 2 Hz while ready is asked for it again") {
    models::L76k chip;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_half_seconds());

    chip.solution_period_ms = models::L76k::kFactoryPeriodMs;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 10000);

    CHECK(chip.sub_second_rate_asked == 2);
    CHECK(chip.solution_period_ms == parts::L76k::kSolutionPeriodMs);
    CHECK(gnss.solving_half_seconds());
}

// The pinned case: a part refusing 2 Hz was left at 1 Hz for good, where ADS-L loses most bursts.
TEST_CASE("l76k: a part that will not solve at 2 Hz is reset and asked again, never left at 1 Hz") {
    models::L76k chip;
    chip.refuses_fast_rate = true;
    chip.reset_pin = 37;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip, &chip, 37);

    run(gnss, chip, 0, 180000);

    CHECK_FALSE(gnss.holds_rate());
    CHECK(chip.hardware_resets >= 2);
    CHECK(chip.sub_second_rate_asked > 2 * parts::L76k::kMaxConfigAttempts);
}

// The pinned case: no time read as off the half seconds, and three tries left the part at 1 Hz.
TEST_CASE("l76k: a part that loses its time stays at 2 Hz, and its fix comes back with the time") {
    models::L76k chip;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    run(gnss, chip, 0, kConfiguredMs);
    REQUIRE(gnss.solving_half_seconds());
    const uint32_t commands = chip.commands_seen;

    chip.time_lost = true;
    run(gnss, chip, kConfiguredMs + 10, kConfiguredMs + 15000);
    CHECK_FALSE(gnss.solution().fix_valid);
    CHECK(gnss.solving_half_seconds());
    CHECK(chip.commands_seen == commands);

    chip.time_lost = false;
    run(gnss, chip, kConfiguredMs + 15010, kConfiguredMs + 20000);
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solving_half_seconds());
    CHECK(chip.solution_period_ms == parts::L76k::kSolutionPeriodMs);
}

// A refusal is not for the session: the part is asked again after its backoff, until it takes 2 Hz.
TEST_CASE(
    "l76k: a part that refused 2 Hz is asked again, and solves twice a second once it takes it") {
    models::L76k chip;
    chip.fastest_interval_ms = parts::L76k::kFactorySolutionPeriodMs;
    parts::L76k gnss(ports::SolutionRate::Half, chip, chip);
    run(gnss, chip, 0, 30000);
    REQUIRE(gnss.recoveries() == 1);
    REQUIRE_FALSE(gnss.holds_rate());

    chip.fastest_interval_ms = 100;
    run(gnss, chip, 30010, 90000);
    CHECK(gnss.holds_rate());
    CHECK(gnss.solving_half_seconds());
}

// The pinned case: a stalled pass drained two pairs and published neither, the newest included.
TEST_CASE("l76k: a stalled pass publishes the newest pair it drains, marked as a backlog") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000") + gga("123520.500", 1005) +
             rmc("123520.500"));
    REQUIRE(gnss.poll(1600));
    CHECK(gnss.solution().fix_valid);
    CHECK(gnss.solution().utc_ms == 500);
    CHECK(gnss.solution().altitude_hae_mm == 1005000 + kGeoidMm);
    CHECK(gnss.solution().backlogged);

    wire.say(gga("123521.000", 1010) + rmc("123521.000"));
    REQUIRE(gnss.poll(1610));
    CHECK(gnss.solution().utc_ms == 0);
    CHECK_FALSE(gnss.solution().backlogged);
}

// Delivery jitters: an epoch landing 240 ms late and the next 70 ms late are 330 ms apart.
TEST_CASE("l76k: pairs delivered 330 ms apart are both published") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000"));
    REQUIRE(gnss.poll(1240));
    wire.say(gga("123520.500", 1005) + rmc("123520.500"));
    REQUIRE(gnss.poll(1570));
    CHECK(gnss.solution().utc_ms == 500);
    CHECK_FALSE(gnss.solution().backlogged);
}

// A pair the driver has already passed in time is old news, whenever it arrives.
TEST_CASE("l76k: a pair no newer than the last one published is not published") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.500", 1000) + rmc("123520.500"));
    REQUIRE(gnss.poll(1000));
    wire.say(gga("123520.000", 1005) + rmc("123520.000"));
    CHECK_FALSE(gnss.poll(1250));
    CHECK(gnss.solution().utc_ms == 500);
    wire.say(gga("123521.000", 1010) + rmc("123521.000"));
    REQUIRE(gnss.poll(1500));
    CHECK(gnss.solution().utc_ms == 0);
}

// The receiver's start and its line time at the live rate bound when a solution can land.
TEST_CASE("l76k: a solution says how late it can land at the rate the port runs at") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000"));
    REQUIRE(gnss.poll(1000));
    CHECK(gnss.solution().delivery_ms ==
          parts::L76k::kBurstStartMs +
              io::wire_ms(parts::L76k::kBurstBytes, parts::L76k::kFactoryBaudRate));
}

// What sat in the port before bring-up belongs to no solution the driver asked for.
TEST_CASE("l76k: what waited in the port before bring-up is thrown away") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000"));
    gnss.service(0);
    gnss.poll(0);
    CHECK_FALSE(gnss.solution().fix_valid);
    CHECK(gnss.sentences() == 0);
}

// A part whose clock steps back, its first fix correcting a guess, is a new time and not a replay.
TEST_CASE("l76k: a stamp that jumps back by more than a replay could is published") {
    ScriptedWire wire;
    parts::L76k gnss(ports::SolutionRate::Half, wire);
    wire.say(gga("123520.000", 1000) + rmc("123520.000"));
    REQUIRE(gnss.poll(1000));
    wire.say(gga("120000.000", 1000) + rmc("120000.000"));
    REQUIRE(gnss.poll(1250));
    CHECK(gnss.solution().utc_ms == 0);
}
