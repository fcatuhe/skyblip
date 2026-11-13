// The transponder Bravo takes for the aircraft's own: the loudest, by a lead, held.
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_bravo/own_transponder.h"
#include "test/support/adsb_frames.h"

using namespace skyblip;
using bravo::OwnTransponder;

namespace {

constexpr uint32_t kOwn = 0x3A4B5C;
constexpr uint32_t kNeighbour = 0x3A0001;
constexpr int8_t kCockpitDbm = -12;
constexpr uint32_t kSquitterMs = 200;
constexpr uint8_t kSetB = 3;
constexpr uint8_t kSetA = 4;
constexpr uint8_t kFirstEmitter = 1;

protocol::AdsbMessage heard(AdsbFrame frame, int8_t rssi_dbm) {
    protocol::AdsbMessage message{};
    REQUIRE(adsb_hear(frame, message) == Status::Ok);
    message.reception.rssi_dbm = rssi_dbm;
    message.reception.rssi_valid = true;
    return message;
}

protocol::AdsbMessage position_of(uint32_t address, int8_t rssi_dbm) {
    return heard(adsb_airborne_position(address, 0, 48.5, 8.5, false, 1500), rssi_dbm);
}

struct Cockpit {
    OwnTransponder own{};
    uint32_t now_ms{1000};

    // The first tick starts the hold, so a hold of n squitters takes n + 1 of them.
    void listen(uint32_t for_ms, int8_t own_dbm, int8_t neighbour_dbm = 0) {
        for (uint32_t spent = 0; spent < for_ms; spent += kSquitterMs) {
            if (own_dbm != 0) own.hear(position_of(kOwn, own_dbm), now_ms);
            if (neighbour_dbm != 0) own.hear(position_of(kNeighbour, neighbour_dbm), now_ms);
            own.tick(now_ms);
            now_ms += kSquitterMs;
        }
    }
};

}  // namespace

TEST_CASE("own transponder: the loudest by a clear lead, held, is named with what it identifies") {
    Cockpit cockpit;
    cockpit.own.hear(
        heard(adsb_identification(kOwn, 0, kSetA, kFirstEmitter, "FJABC"), kCockpitDbm),
        cockpit.now_ms);
    cockpit.listen(OwnTransponder::kLeadHoldMs - kSquitterMs, kCockpitDbm, -60);
    CHECK_FALSE(cockpit.own.heard().has_value());

    cockpit.listen(2 * kSquitterMs, kCockpitDbm, -60);
    const auto named = cockpit.own.heard();
    REQUIRE(named.has_value());
    CHECK(named->address == kOwn);
    CHECK(std::string(named->callsign) == "FJABC");
    CHECK(named->aircraft_category == 1);
}

TEST_CASE("own transponder: the identification sets the category the emitter set names") {
    Cockpit cockpit;
    cockpit.own.hear(
        heard(adsb_identification(kOwn, 0, kSetB, kFirstEmitter, "D1234"), kCockpitDbm),
        cockpit.now_ms);
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, kCockpitDbm);
    REQUIRE(cockpit.own.heard().has_value());
    CHECK(cockpit.own.heard()->aircraft_category == 4);
}

// A neighbour on the apron as loud as our own is a guess, and the device makes none.
TEST_CASE("own transponder: two transponders inside the lead of each other name neither") {
    Cockpit cockpit;
    cockpit.listen(3 * OwnTransponder::kLeadHoldMs, kCockpitDbm,
                   kCockpitDbm - OwnTransponder::kLeadDb + 1);
    CHECK_FALSE(cockpit.own.heard().has_value());
}

TEST_CASE("own transponder: one under the floor is never named, however alone it is") {
    Cockpit cockpit;
    cockpit.listen(3 * OwnTransponder::kLeadHoldMs, OwnTransponder::kLoudFloorDbm - 1);
    CHECK_FALSE(cockpit.own.heard().has_value());
}

TEST_CASE("own transponder: a new leader holds from the start, and a quiet one is let go") {
    Cockpit cockpit;
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, kCockpitDbm, -60);
    REQUIRE(cockpit.own.heard().has_value());

    cockpit.listen(OwnTransponder::kEmitterMaxAgeMs + kSquitterMs, 0, -20);
    CHECK_FALSE(cockpit.own.heard().has_value());
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, 0, -20);
    REQUIRE(cockpit.own.heard().has_value());
    CHECK(cockpit.own.heard()->address == kNeighbour);

    cockpit.listen(OwnTransponder::kEmitterMaxAgeMs + kSquitterMs, 0);
    CHECK_FALSE(cockpit.own.heard().has_value());
}

// One loud frame from a passing aircraft moves the level a quarter of the way, not all of it.
TEST_CASE("own transponder: a single frame of a neighbour does not break the lead") {
    Cockpit cockpit;
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, kCockpitDbm, -60);
    REQUIRE(cockpit.own.heard().has_value());
    cockpit.own.hear(position_of(kNeighbour, kCockpitDbm), cockpit.now_ms);
    cockpit.listen(kSquitterMs, kCockpitDbm);
    CHECK(cockpit.own.heard().has_value());
}

TEST_CASE("own transponder: a frame with no level of its own is no evidence of loudness") {
    Cockpit cockpit;
    for (int i = 0; i < 100; i++) {
        protocol::AdsbMessage message = position_of(kOwn, kCockpitDbm);
        message.reception.rssi_valid = false;
        cockpit.own.hear(message, cockpit.now_ms);
        cockpit.own.tick(cockpit.now_ms);
        cockpit.now_ms += kSquitterMs;
    }
    CHECK_FALSE(cockpit.own.heard().has_value());
}

TEST_CASE("own transponder: address 000000 is never named, since as own ICAO it means unset") {
    Cockpit cockpit;
    for (int i = 0; i < 100; i++) {
        cockpit.own.hear(position_of(0, kCockpitDbm), cockpit.now_ms);
        cockpit.own.tick(cockpit.now_ms);
        cockpit.now_ms += kSquitterMs;
    }
    CHECK_FALSE(cockpit.own.heard().has_value());
}

// A slot heard last 2^32 ms ago would read live again, so a quiet one must be freed.
TEST_CASE("own transponder: an emitter gone quiet is forgotten across the clock's wrap") {
    Cockpit cockpit;
    cockpit.now_ms = 0xFFFFFFFFu - 60000;
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, kCockpitDbm);
    REQUIRE(cockpit.own.heard().has_value());
    cockpit.listen(OwnTransponder::kEmitterMaxAgeMs + kSquitterMs, 0);
    REQUIRE_FALSE(cockpit.own.heard().has_value());
    cockpit.now_ms += 0xFFFFFFFFu - 2 * OwnTransponder::kEmitterMaxAgeMs;
    cockpit.listen(OwnTransponder::kLeadHoldMs + kSquitterMs, 0);
    CHECK_FALSE(cockpit.own.heard().has_value());
}
