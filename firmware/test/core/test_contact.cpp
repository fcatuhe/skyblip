// A contact, conditioned: one settled edge per thing a finger did, stamped when the level changed.
#include "core/input/contact.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {

constexpr uint32_t kSettleMs = 30;

using Edge = input::Contact::Edge;

Edge step(input::Contact& contact, bool level, uint32_t& now_ms, uint32_t for_ms) {
    Edge seen = Edge::None;
    for (uint32_t elapsed = 0; elapsed <= for_ms; elapsed += 10) {
        const Edge edge = contact.update(level, now_ms + elapsed);
        if (edge != Edge::None) seen = edge;
    }
    now_ms += for_ms;
    return seen;
}

}  // namespace

TEST_CASE("contact: one down and one up per press, whatever the level did between") {
    input::Contact contact{kSettleMs};
    uint32_t now_ms = 1000;
    CHECK(step(contact, true, now_ms, 200) == Edge::Down);
    CHECK(contact.down());
    CHECK(step(contact, true, now_ms, 5000) == Edge::None);
    CHECK(step(contact, false, now_ms, 200) == Edge::Up);
    CHECK_FALSE(contact.down());
    CHECK(step(contact, false, now_ms, 5000) == Edge::None);
}

TEST_CASE("contact: bounce inside the settle window is not an edge") {
    input::Contact contact{kSettleMs};
    uint32_t now_ms = 0;
    const bool chatter[] = {true, false, true, false, true};
    for (bool level : chatter) {
        CHECK(contact.update(level, now_ms) == Edge::None);
        now_ms += 1;
    }
    CHECK_FALSE(contact.down());
    CHECK(step(contact, true, now_ms, 200) == Edge::Down);
}

TEST_CASE("contact: a spike shorter than the settle is ignored entirely") {
    input::Contact contact{kSettleMs};
    contact.update(false, 0);
    CHECK(contact.update(true, 100) == Edge::None);
    CHECK(contact.update(false, 100 + kSettleMs - 10) == Edge::None);
    uint32_t now_ms = 110;
    CHECK(step(contact, false, now_ms, 500) == Edge::None);
    CHECK_FALSE(contact.down());
}

// A late poll timed at the poll turns a long touch into a tap, and steals the way home.
TEST_CASE("contact: an edge is stamped when the level changed, not when it was seen") {
    input::Contact contact{kSettleMs};
    contact.update(true, 1000);
    REQUIRE(contact.update(true, 1000 + kSettleMs) == Edge::Down);
    CHECK(contact.edge_ms() == 1000);

    CHECK(contact.update(false, 4000) == Edge::None);
    REQUIRE(contact.update(false, 9000) == Edge::Up);
    CHECK(contact.edge_ms() == 4000);
}

TEST_CASE("contact: the settle window is the board's, so two contacts can differ") {
    input::Contact quick{5};
    input::Contact slow{200};
    uint32_t now_ms = 0;
    CHECK(step(quick, true, now_ms, 50) == Edge::Down);
    uint32_t slow_now_ms = 0;
    CHECK(step(slow, true, slow_now_ms, 50) == Edge::None);
    CHECK(step(slow, true, slow_now_ms, 200) == Edge::Down);
}
