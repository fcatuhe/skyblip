// Both platforms drive this object, so the lifecycle rules are proved here once.
#include "core/comms/link_sessions.h"
#include "core/events/link.h"
#include "doctest/doctest.h"

using namespace skyblip;
using comms::LinkSessions;
using events::LinkEvent;
using events::LinkEventType;

TEST_CASE("link sessions: a connect and a disconnect arrive as one ordered pair") {
    LinkSessions sessions;
    CHECK_FALSE(sessions.up());

    LinkEvent event{};
    CHECK_FALSE(sessions.pop(event));

    sessions.connected(0x4231, 244);
    CHECK(sessions.up());
    CHECK(sessions.up(0x4231));
    CHECK(sessions.count() == 1);

    REQUIRE(sessions.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 0x4231);
    CHECK(event.payload_bytes == 244);
    CHECK_FALSE(sessions.pop(event));

    sessions.disconnected(0x4231);
    CHECK_FALSE(sessions.up());
    REQUIRE(sessions.pop(event));
    CHECK(event.type == LinkEventType::Down);
    CHECK(event.session_id == 0x4231);
    CHECK_FALSE(sessions.pop(event));
}

// An iOS central connects first and exchanges the MTU afterwards.
TEST_CASE("link sessions: a late MTU exchange refreshes the session it belongs to") {
    LinkSessions sessions;
    sessions.connected(7, ports::kMinimumLinkPayloadBytes);
    LinkEvent event{};
    REQUIRE(sessions.pop(event));
    CHECK(event.payload_bytes == ports::kMinimumLinkPayloadBytes);

    sessions.payload_changed(7, 182);
    REQUIRE(sessions.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 7);
    CHECK(event.payload_bytes == 182);
    CHECK(sessions.payload_bytes() == 182);

    sessions.payload_changed(7, 182);
    CHECK_FALSE(sessions.pop(event));

    sessions.disconnected(7);
    REQUIRE(sessions.pop(event));
    sessions.payload_changed(7, 247);
    CHECK_FALSE(sessions.pop(event));
}

TEST_CASE("link sessions: a payload below what BLE guarantees is floored, never carried") {
    LinkSessions sessions;
    sessions.connected(1, 0);
    LinkEvent event{};
    REQUIRE(sessions.pop(event));
    CHECK(event.payload_bytes == ports::kMinimumLinkPayloadBytes);
    CHECK(sessions.payload_bytes() == ports::kMinimumLinkPayloadBytes);

    sessions.payload_changed(1, 4);
    CHECK_FALSE(sessions.pop(event));
}

TEST_CASE("link sessions: a disconnect for a session that was never up is ignored") {
    LinkSessions sessions;
    sessions.connected(3, 244);
    LinkEvent event{};
    REQUIRE(sessions.pop(event));

    sessions.disconnected(9);
    CHECK(sessions.up());
    CHECK_FALSE(sessions.pop(event));

    sessions.disconnected(3);
    REQUIRE(sessions.pop(event));
    sessions.disconnected(3);
    CHECK_FALSE(sessions.pop(event));
}

// The whole point of the table: a yoke tablet and a pocket phone, neither closing the other.
TEST_CASE("link sessions: a second central joins, and the first one stays up") {
    LinkSessions sessions;
    sessions.connected(1, 244);
    sessions.connected(2, 182);
    CHECK(sessions.count() == 2);
    CHECK(sessions.up(1));
    CHECK(sessions.up(2));

    LinkEvent event{};
    REQUIRE(sessions.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 1);
    REQUIRE(sessions.pop(event));
    CHECK(event.type == LinkEventType::Up);
    CHECK(event.session_id == 2);
    CHECK_FALSE(sessions.pop(event));

    sessions.disconnected(1);
    CHECK(sessions.up());
    CHECK(sessions.count() == 1);
}

// One NMEA frame is formatted once for every central, so it has to fit the narrowest.
TEST_CASE("link sessions: the payload figure is the smallest central connected") {
    LinkSessions sessions;
    CHECK(sessions.payload_bytes() == ports::kMinimumLinkPayloadBytes);

    sessions.connected(1, 244);
    CHECK(sessions.payload_bytes() == 244);

    sessions.connected(2, 182);
    CHECK(sessions.payload_bytes() == 182);
    CHECK(sessions.payload_bytes(1) == 244);
    CHECK(sessions.payload_bytes(2) == 182);

    sessions.disconnected(2);
    CHECK(sessions.payload_bytes() == 244);
}

// A central the table has no room for must not become a session a service answers.
TEST_CASE("link sessions: a central beyond the table is refused and counted") {
    LinkSessions sessions;
    for (uint16_t i = 1; i <= LinkSessions::kMaxSessions; i++) sessions.connected(i, 244);
    CHECK(sessions.count() == static_cast<int>(LinkSessions::kMaxSessions));

    sessions.connected(99, 244);
    CHECK(sessions.refused() == 1);
    CHECK_FALSE(sessions.up(99));
    CHECK(sessions.count() == static_cast<int>(LinkSessions::kMaxSessions));
}

TEST_CASE("link sessions: a lifecycle event that does not fit is counted, not lost quietly") {
    LinkSessions sessions;
    for (uint16_t round = 0; round < LinkSessions::kMaxEvents; round++) {
        sessions.connected(1, 244);
        sessions.disconnected(1);
    }
    CHECK(sessions.dropped() > 0);
    CHECK_FALSE(sessions.up());
}

namespace {

LinkEvent down_of(LinkSessions& sessions, uint16_t session_id) {
    LinkEvent event{};
    while (sessions.pop(event)) {
    }
    sessions.disconnected(session_id);
    REQUIRE(sessions.pop(event));
    REQUIRE(event.type == LinkEventType::Down);
    return event;
}

}  // namespace

// The measure behind small_mtu: which side started the exchange, and what the link carried.
TEST_CASE("link sessions: a link nobody exchanged ends at twenty bytes, neither side named") {
    LinkSessions sessions;
    sessions.connected(1, ports::kMinimumLinkPayloadBytes);
    sessions.mtu_requested(1, events::MtuRequest::Refused, 0x06);
    const LinkEvent down = down_of(sessions, 1);
    CHECK(down.payload_bytes == ports::kMinimumLinkPayloadBytes);
    CHECK(down.mtu_exchange_started_by == events::MtuExchangeStarter::None);
}

TEST_CASE("link sessions: an exchange the unit did not answer for is the central's") {
    LinkSessions sessions;
    sessions.connected(1, ports::kMinimumLinkPayloadBytes);
    sessions.mtu_requested(1, events::MtuRequest::Pending);
    sessions.payload_changed(1, 182);
    sessions.mtu_requested(1, events::MtuRequest::AlreadyExchanged);
    const LinkEvent down = down_of(sessions, 1);
    CHECK(down.payload_bytes == 182);
    CHECK(down.mtu_exchange_started_by == events::MtuExchangeStarter::Central);

    sessions.connected(2, 244);
    CHECK(down_of(sessions, 2).mtu_exchange_started_by == events::MtuExchangeStarter::Central);
}

// Zephyr calls att_mtu_updated for the unit's own exchange too, just before its response callback.
TEST_CASE("link sessions: the unit's own exchange is the unit's, and a crossed one is both") {
    LinkSessions alone;
    alone.connected(1, ports::kMinimumLinkPayloadBytes);
    alone.mtu_requested(1, events::MtuRequest::Pending);
    alone.payload_changed(1, 244);
    alone.mtu_requested(1, events::MtuRequest::Exchanged);
    const LinkEvent by_unit = down_of(alone, 1);
    CHECK(by_unit.payload_bytes == 244);
    CHECK(by_unit.mtu_exchange_started_by == events::MtuExchangeStarter::Unit);

    LinkSessions crossed;
    crossed.connected(1, ports::kMinimumLinkPayloadBytes);
    crossed.mtu_requested(1, events::MtuRequest::Pending);
    crossed.payload_changed(1, 182);
    crossed.payload_changed(1, 182);
    crossed.mtu_requested(1, events::MtuRequest::Exchanged);
    const LinkEvent both = down_of(crossed, 1);
    CHECK(both.mtu_exchange_started_by == events::MtuExchangeStarter::Both);
    CHECK(both.payload_bytes == 182);
}
