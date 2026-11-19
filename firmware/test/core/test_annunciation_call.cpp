// A paired Bravo plays the calls its Go grades, and its own press holds until the Go answers it.
#include <optional>

#include "core/annunciation/call.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::annunciation;
using skyblip::traffic::Level;

namespace {

Call advisory(uint32_t announcement) {
    return Call{Level::Advisory, Level::Advisory, announcement};
}

constexpr Call kQuiet{Level::None, Level::None, 0};

}  // namespace

TEST_CASE("annunciation call: a follower plays the leader's level, and calls only a new number" *
          doctest::test_suite("main")) {
    Follower follower;
    const Follower::Heard first = follower.follow(advisory(4));
    CHECK(first.level == Level::Advisory);
    CHECK(first.undismissed == Level::Advisory);
    CHECK_FALSE(first.announced);

    CHECK_FALSE(follower.follow(advisory(4)).announced);
    const Follower::Heard second = follower.follow(advisory(5));
    CHECK(second.announced);
    CHECK(second.level == Level::Advisory);
    CHECK_FALSE(follower.follow(advisory(5)).announced);
}

// The leader numbers its calls from 0 after a boot, so a lost link starts the count again.
TEST_CASE("annunciation call: a follower that lost its leader calls nothing on the first call") {
    Follower follower;
    (void)follower.follow(advisory(7));
    follower.lose();
    const Follower::Heard again = follower.follow(advisory(1));
    CHECK_FALSE(again.announced);
    CHECK(again.level == Level::Advisory);
}

TEST_CASE("annunciation call: a press holds the call it heard, and a new call ends the hold") {
    Follower follower;
    (void)follower.follow(advisory(3));
    follower.dismiss();
    REQUIRE(follower.dismissed() == std::optional<uint32_t>(3));

    const Follower::Heard held = follower.follow(advisory(3));
    CHECK(held.level == Level::None);
    CHECK(held.undismissed == Level::None);
    CHECK_FALSE(held.announced);

    const Follower::Heard next = follower.follow(advisory(4));
    CHECK(next.announced);
    CHECK(next.level == Level::Advisory);
    CHECK(next.undismissed == Level::Advisory);
    CHECK(follower.dismissed() == std::optional<uint32_t>(3));
}

TEST_CASE("annunciation call: a press with no leader holds nothing, and a lost leader forgets it") {
    Follower follower;
    follower.dismiss();
    CHECK_FALSE(follower.dismissed().has_value());

    (void)follower.follow(advisory(2));
    follower.dismiss();
    REQUIRE(follower.dismissed().has_value());
    follower.lose();
    CHECK_FALSE(follower.dismissed().has_value());
    CHECK(follower.follow(advisory(2)).level == Level::Advisory);
}

TEST_CASE("annunciation call: a quiet leader is followed as quiet") {
    Follower follower;
    const Follower::Heard heard = follower.follow(kQuiet);
    CHECK(heard.level == Level::None);
    CHECK(heard.undismissed == Level::None);
    CHECK_FALSE(heard.announced);
}
