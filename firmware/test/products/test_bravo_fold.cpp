// How a paired Go places each Bravo observation in time, and the frames it refuses to place.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/events/link.h"
#include "core/settings/address.h"
#include "core/traffic/alarm.h"
#include "doctest/doctest.h"
#include "test/support/bravo_flight.h"
#include "test/support/diagnostics_corpus.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

constexpr uint32_t kOtherTarget = 0x4459AA;

std::vector<BravoStub::Recorded> every_second(uint32_t from, uint32_t to, uint32_t into_ms,
                                              const std::string& fields) {
    std::vector<BravoStub::Recorded> recording;
    for (uint32_t second = from; second < to; second++)
        recording.push_back({edge_ms_of(second) + into_ms, fields});
    return recording;
}

events::RxFrame peer_frame(const std::string& json) {
    const std::string bytes = complete_registry_frame(json);
    events::RxFrame frame{};
    frame.session_id = 1;
    frame.connection = 1;
    frame.endpoint = events::Endpoint::Peer;
    frame.bytes = static_cast<uint16_t>(bytes.size());
    std::memcpy(frame.data.data(), bytes.data(), bytes.size());
    return frame;
}

}  // namespace

// Two clocks a second apart may date a field a little after our now, and it keeps that instant.
TEST_CASE("bravo fold: a frame up to a second ahead keeps its own instant, forwarded or not") {
    Flight flight({{edge_ms_of(2) + 300, level_target(3, 0, 1500)},
                   {edge_ms_of(2) + 800, level_target(3, 0, 1500)},
                   {edge_ms_of(3) + 400, level_target(3, 0, 1500)}});
    flight.fly(3);
    const traffic::Target* ahead = flight.target();
    REQUIRE(ahead != nullptr);
    CHECK(ahead->observation.at_ms == edge_ms_of(3));
    CHECK(ahead->observation.received.at_s == utc_of(3));

    flight.fly(1);
    REQUIRE(flight.target() != nullptr);
    CHECK(flight.target()->observation.at_ms == edge_ms_of(3));
    CHECK(flight.tango.service().unplaced() == 0);

    // Heard at second 3, an airborne target's six-second lease ends at second 10.
    flight.fly(6);
    CHECK(flight.target() != nullptr);
    flight.fly(1);
    CHECK(flight.target() == nullptr);
}

TEST_CASE("bravo fold: a frame dated further ahead than a second is refused and counted") {
    std::vector<BravoStub::Recorded> recording =
        every_second(2, 12, 0, level_target(3602, 0, 1500));
    recording.push_back({edge_ms_of(12) + 50, level_target(13, 200, 1500)});
    Flight flight(recording);
    flight.fly(14);
    CHECK(flight.target() == nullptr);
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().unplaced() == 11);
}

TEST_CASE("bravo fold: with no PPS, the fix that named the second dates the frame") {
    Flight flight({{edge_ms_of(2) + 500, level_target(2, 200, 1500)}});
    flight.go().platform.pps().set_locked(false);
    flight.fly(4);
    REQUIRE_FALSE(flight.state().clock.pps_locked);
    const traffic::Target* target = flight.target();
    REQUIRE(target != nullptr);
    CHECK(target->observation.at_ms == edge_ms_of(2) + 200);
}

TEST_CASE("bravo fold: a Go with no UTC of its own folds nothing, and counts it") {
    Flight flight({{kFlyFromMs - 2000, level_target(0, 0, 1500)}});
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().unplaced() == 1);
}

TEST_CASE("bravo fold: a frame from a Bravo whose link is down is never folded") {
    Flight flight;
    flight.fly(2);
    flight.tango.air.set_in_range(false);
    flight.tango.air.drop();
    flight.fly(1);
    flight.go().product.bus().peer_rx.push(
        peer_frame("{\"command\":\"observation\",\"boot_epoch\":1,\"sequence\":0," +
                   level_target(3, 0, 800) + "}"));
    flight.fly(1);
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().unplaced() == 0);
    CHECK(flight.tango.service().refused() == 0);
}

// #192: a paired Go reads CONNECTED, and folds, only once Bravo's health says it holds the pin.
TEST_CASE("bravo fold: a reconnected Go folds nothing before Bravo's health says it is paired") {
    Flight flight;
    flight.fly(2);
    flight.tango.air.set_flow(PairedLink::Direction::ToCentral, PairedLink::Flow::Stalled);
    flight.tango.air.drop();
    flight.fly(2);
    REQUIRE(flight.go().platform.central().up());
    REQUIRE(flight.tango.service().link() != go::BravoLink::Up);
    flight.go().product.bus().peer_rx.push(
        peer_frame("{\"command\":\"observation\",\"boot_epoch\":1,\"sequence\":0," +
                   level_target(4, 0, 800) + "}"));
    flight.fly(1);
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().refused() == 0);

    flight.tango.air.set_flow(PairedLink::Flow::Flowing);
    flight.fly(1);
    CHECK(flight.tango.service().link() == go::BravoLink::Up);
}

TEST_CASE("bravo fold: an observation the reader refuses is counted, and folds nothing") {
    Flight flight({{edge_ms_of(2) + 300, "\"address\":" + std::to_string(kTarget) +
                                             ",\"at_s\":" + std::to_string(utc_of(2)) +
                                             ",\"latitude_1e7\":485000000"}});
    flight.fly(3);
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().refused() == 1);
}

// A height no ADS-B field encodes overflowed the alarm's millimetres once own-ship had a fix.
TEST_CASE("bravo fold: an altitude out of ADS-B's range is refused before own-ship has a fix") {
    Flight flight;
    gnss::GnssSolution timed{};
    timed.utc_valid = true;
    timed.utc_s = utc_of(0);
    timed.sentences = 1;
    flight.go().product.bus().gnss.push(timed);
    flight.go().run(flight.tango.now_ms, flight.tango.now_ms + 100);
    REQUIRE(flight.state().own.utc_valid);
    REQUIRE_FALSE(flight.state().own.fix_valid);
    flight.go().product.bus().peer_rx.push(
        peer_frame("{\"command\":\"observation\",\"boot_epoch\":1,\"sequence\":0," +
                   heard_at(0, 0, 1500) + ",\"address\":" + std::to_string(kTarget) +
                   ",\"altitude_m\":2147483647,\"altitude_age_ms\":0}"));
    flight.go().run(flight.tango.now_ms + 150, flight.tango.now_ms + 400);
    CHECK(flight.state().traffic.count() == 0);
    CHECK(flight.tango.service().refused() == 1);

    flight.tango.now_ms = edge_ms_of(1);
    flight.go().utc_offset_s = 1;
    flight.fly(3);
    REQUIRE(flight.state().own.fix_valid);
    CHECK(flight.state().traffic.count() == 0);
}

TEST_CASE("bravo fold: a callsign keeps the second Bravo heard it, else the frame's own") {
    const std::string named_long_ago =
        level_target(2, 0, 1500,
                     ",\"callsign\":\"DLH4AB\",\"callsign_heard_s\":" +
                         std::to_string(utc_of(2) - traffic::kCallsignForgetS + 1));
    const std::string named_undated = "\"address\":" + std::to_string(kOtherTarget) + "," +
                                      heard_at(2, 0, 2000) +
                                      ",\"altitude_m\":" + std::to_string(kAltitudeHaeM) +
                                      ",\"altitude_age_ms\":0,\"callsign\":\"AFR12\"";
    Flight flight({{edge_ms_of(2) + 300, named_long_ago}, {edge_ms_of(2) + 400, named_undated}});
    flight.fly(4);
    const traffic::CallsignTable& names = flight.state().callsigns;
    CHECK(names.find(adsl::kAddressTableIcao, kTarget) != nullptr);
    flight.fly(1);
    CHECK(names.find(adsl::kAddressTableIcao, kTarget) == nullptr);
    CHECK(names.find(adsl::kAddressTableIcao, kOtherTarget) != nullptr);
}

TEST_CASE("bravo fold: an armed capture records each folded ADS-B aircraft") {
    Flight flight({{edge_ms_of(3) + 300, level_target(3, 0, 1500)}});
    flight.go().product.diagnostics().arm();
    flight.fly(6);
    REQUIRE(flight.go().product.capture().capturing());
    int recorded = 0;
    for (const diagnostics::Record& record : captured(flight.go())) {
        diagnostics::Adsb adsb{};
        if (record.type == diagnostics::Type::Adsb && diagnostics::read(record, adsb) &&
            adsb.address == kTarget)
            recorded++;
    }
    CHECK(recorded == 1);
}

// A reception's alarm is as old as the instant it was true, not the instant it was forwarded.
TEST_CASE("bravo fold: a target forwarded late stops sounding with its own age, not its arrival") {
    Flight flight({{edge_ms_of(21) + 500, level_target(16, 600, 1500)}});
    flight.fly(22);
    REQUIRE(flight.target() != nullptr);

    // At second 24.95 the reception is 8.35 s old: two seconds past its lease, 3.45 s after
    // arrival.
    flight.fly(3);
    CHECK(flight.target() == nullptr);
    CHECK(flight.go().product.alarm().announcing_level() == traffic::Level::None);
}
