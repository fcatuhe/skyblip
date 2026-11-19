// §D.3's media access, one branch a case: what a busy channel does to a packet waiting to go out.
#include <algorithm>
#include <cstdint>

#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
#include "protocols/common/en300220.h"

using namespace skyblip;
using namespace skyblip::adsl;

namespace {

constexpr uint32_t kUnitUs = 5000;
constexpr uint64_t kAtUs = 10000000;

ChannelAttempt busy_at(uint64_t at_us) {
    ChannelAttempt attempt{};
    attempt.at_us = at_us;
    attempt.first_us = at_us;
    attempt.earliest_us = at_us;
    attempt.last_us = at_us + 100000;
    return attempt;
}

Deferral deferral(uint32_t draw, uint32_t units = 8) { return Deferral{kUnitUs, units, draw}; }

}  // namespace

TEST_CASE("adsl media access: a clear channel transmits at the instant it was assessed" *
          doctest::test_suite("main")) {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.clear = true;
    const AccessVerdict verdict = media_access(attempt, deferral(0));
    CHECK(verdict.access == Access::Transmit);
    CHECK(verdict.at_us == kAtUs);
}

TEST_CASE("adsl media access: a busy channel defers by a drawn whole number of units") {
    for (uint32_t draw = 0; draw < 16; draw++) {
        CAPTURE(draw);
        const AccessVerdict verdict = media_access(busy_at(kAtUs), deferral(draw));
        REQUIRE(verdict.access == Access::Defer);
        CHECK((verdict.at_us - kAtUs) % kUnitUs == 0);
        CHECK(verdict.at_us - kAtUs == (3 + draw % 6) * kUnitUs);
    }
}

// C.2's 15 ms floor is three 5 ms units, so a caller declaring two never transmits after a busy
// one.
TEST_CASE("adsl media access: a caller whose units fall short of the band's floor misses") {
    CHECK(media_access(busy_at(kAtUs), deferral(0, 2)).access == Access::Missed);
    CHECK(media_access(busy_at(kAtUs), deferral(0, 3)).at_us == kAtUs + 3 * kUnitUs);
}

TEST_CASE("adsl media access: no deferral reaches past the band's ceiling, whatever is declared") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.last_us = kAtUs + 1000000;
    uint64_t longest_us = 0;
    for (uint32_t draw = 0; draw < 100; draw++) {
        const AccessVerdict verdict = media_access(attempt, deferral(draw, 100));
        REQUIRE(verdict.access == Access::Defer);
        longest_us = std::max<uint64_t>(longest_us, verdict.at_us - kAtUs);
    }
    CHECK(longest_us == kMBandBackoff.ceiling_us);
}

// A ground station on the O band backs off over C.4's 10 to 50 ms instead.
TEST_CASE("adsl media access: the O band backs off between its own 10 and 50 ms") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.backoff = kOBandBackoff;
    for (uint32_t draw = 0; draw < 32; draw++) {
        const AccessVerdict verdict = media_access(attempt, deferral(draw, 100));
        REQUIRE(verdict.access == Access::Defer);
        CHECK(verdict.at_us - kAtUs >= kOBandBackoff.floor_us);
        CHECK(verdict.at_us - kAtUs <= kOBandBackoff.ceiling_us);
    }
}

// The device hears of the busy channel a pass after it was assessed, and cannot listen before then.
TEST_CASE("adsl media access: a deferral lands no earlier than the device can listen again") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.earliest_us = kAtUs + 12000;
    const AccessVerdict verdict = media_access(attempt, deferral(0));
    REQUIRE(verdict.access == Access::Defer);
    CHECK(verdict.at_us == kAtUs + 3 * kUnitUs);
}

// The device hears late, so the draw keeps to what is left of its units and never waits past them.
TEST_CASE("adsl media access: a deferral never waits more units than the caller declares") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.earliest_us = kAtUs + 36000;
    for (uint32_t draw = 0; draw < 16; draw++) {
        const AccessVerdict verdict = media_access(attempt, deferral(draw));
        REQUIRE(verdict.access == Access::Defer);
        CHECK(verdict.at_us == kAtUs + 8 * kUnitUs);
    }
    attempt.earliest_us = kAtUs + 8 * kUnitUs + 1;
    CHECK(media_access(attempt, deferral(0)).access == Access::Missed);
}

TEST_CASE("adsl media access: the draw never reaches past the window the packet must start in") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.last_us = kAtUs + 4 * kUnitUs + 1;
    for (uint32_t draw = 0; draw < 16; draw++) {
        const AccessVerdict verdict = media_access(attempt, deferral(draw));
        REQUIRE(verdict.access == Access::Defer);
        CHECK(verdict.at_us <= attempt.last_us);
    }
}

TEST_CASE("adsl media access: a window with no whole unit left is missed") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.last_us = kAtUs + kUnitUs - 1;
    CHECK(media_access(attempt, deferral(0)).access == Access::Missed);
    attempt.last_us = kAtUs;
    CHECK(media_access(attempt, deferral(0)).access == Access::Missed);
    attempt.last_us = kAtUs + 3 * kUnitUs;
    attempt.earliest_us = kAtUs + 3 * kUnitUs + 1;
    CHECK(media_access(attempt, deferral(0)).access == Access::Missed);
}

TEST_CASE("adsl media access: a unit is never shorter than EN 300 220-1's minimum period") {
    ChannelAttempt attempt = busy_at(kAtUs);
    const AccessVerdict verdict = media_access(attempt, Deferral{0, 200, 0});
    REQUIRE(verdict.access == Access::Defer);
    CHECK((verdict.at_us - kAtUs) % en300220::kDeferralFloorUs == 0);
    CHECK(verdict.at_us - kAtUs >= kMBandBackoff.floor_us);
    CHECK(media_access(attempt, Deferral{kUnitUs, 0, 7}).access == Access::Missed);
}

TEST_CASE("adsl media access: 3000 ms after the initial attempt a busy channel is forced") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.first_us = kAtUs - kForceAfterAttemptUs;
    attempt.may_force = true;
    attempt.earliest_us = kAtUs + 1;
    const AccessVerdict verdict = media_access(attempt, deferral(5));
    CHECK(verdict.access == Access::Force);
    CHECK(verdict.at_us == kAtUs + 3 * kUnitUs);

    attempt.first_us = kAtUs - kForceAfterAttemptUs + 1;
    CHECK(media_access(attempt, deferral(5)).access == Access::Defer);
}

TEST_CASE("adsl media access: a packet that may not be forced keeps deferring") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.first_us = kAtUs - 2 * kForceAfterAttemptUs;
    CHECK(media_access(attempt, deferral(0)).access == Access::Defer);
}

TEST_CASE("adsl media access: a force with no unit left in the window is missed") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.first_us = kAtUs - kForceAfterAttemptUs;
    attempt.may_force = true;
    attempt.last_us = kAtUs + kUnitUs - 1;
    CHECK(media_access(attempt, deferral(0)).access == Access::Missed);
}

TEST_CASE("adsl media access: nothing goes out for 2000 ms after a forced transmission ends") {
    ChannelAttempt attempt = busy_at(kAtUs);
    attempt.clear = true;
    attempt.forced_end_us = kAtUs - kSilenceAfterForceUs + 1;
    CHECK(media_access(attempt, deferral(0)).access == Access::Silent);
    attempt.clear = false;
    attempt.may_force = true;
    attempt.first_us = kAtUs - kForceAfterAttemptUs;
    CHECK(media_access(attempt, deferral(0)).access == Access::Silent);
    attempt.forced_end_us = kAtUs - kSilenceAfterForceUs;
    CHECK(media_access(attempt, deferral(0)).access == Access::Force);
}
