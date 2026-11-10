// Two apps on one device: the picture is everyone's, the settings are one app's.
#include "core/comms/link_claim.h"
#include "doctest/doctest.h"

using namespace skyblip;
using comms::LinkClaim;
using events::Peer;

TEST_CASE("link claim: the first session to ask holds it, and the next one is refused") {
    LinkClaim claim;
    CHECK_FALSE(claim.held());

    CHECK(claim.grant(Peer{7, 1}));
    CHECK(claim.held());
    CHECK(claim.holder() == 7);

    CHECK_FALSE(claim.grant(Peer{9, 2}));
    CHECK(claim.holder() == 7);
    CHECK(claim.holds(7));
    CHECK_FALSE(claim.holds(9));

    // The holder asking again is the same app sending its next command.
    CHECK(claim.grant(Peer{7, 1}));
}

// Silicon reuses a session id for the next phone, so the id alone is not the app that asked.
TEST_CASE("link claim: the same session id on another connection is not the holder") {
    LinkClaim claim;
    REQUIRE(claim.grant(Peer{1, 1}));
    CHECK_FALSE(claim.grant(Peer{1, 2}));
    CHECK(claim.holds(Peer{1, 1}));
    CHECK_FALSE(claim.holds(Peer{1, 2}));
    CHECK(claim.peer() == Peer{1, 1});
}

TEST_CASE("link claim: only the holder can release it") {
    LinkClaim claim;
    REQUIRE(claim.grant(Peer{1, 1}));

    claim.release(2);
    CHECK(claim.holds(1));

    claim.release(1);
    CHECK_FALSE(claim.held());
    CHECK(claim.grant(Peer{2, 2}));
}
