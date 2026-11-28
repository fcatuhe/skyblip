// What is aboard our own aircraft: the loudest emitter by a clear lead, held, and the FLARM ID.
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/traffic/aboard.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "protocols/adsl/packet.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

constexpr AboardTuning kTuning{-50, 10, 3000, 2500};
constexpr uint32_t kOwn = 1;
constexpr uint32_t kNeighbour = 2;
constexpr uint32_t kStepMs = 500;

struct Tag {
    int frames{0};
};

using Detector = Aboard<Tag, 2>;

uint32_t named(const Detector& detector) {
    const Detector::Emitter* emitter = detector.held();
    return emitter == nullptr ? 0 : emitter->key;
}

// Every half second both emitters are heard, if loud at all, then the detector ticks.
void listen(Detector& detector, uint32_t& now_ms, uint32_t for_ms, int8_t own_dbm,
            int8_t neighbour_dbm) {
    for (uint32_t spent = 0; spent < for_ms; spent += kStepMs) {
        if (own_dbm != 0) (void)detector.hear(kOwn, true, own_dbm, now_ms);
        if (neighbour_dbm != 0) (void)detector.hear(kNeighbour, true, neighbour_dbm, now_ms);
        detector.tick(now_ms);
        now_ms += kStepMs;
    }
}

model::AircraftObservation flarm(uint32_t address, uint8_t address_mapping_table) {
    model::AircraftObservation observation = target_at(flying(30, 0), 1000, 0, 0, 30, 0);
    observation.address = address;
    observation.address_mapping_table = address_mapping_table;
    observation.source = model::Source::Alptas;
    observation.received.at_s = 100;
    return observation;
}

}  // namespace

TEST_CASE("aboard: the loudest by a clear lead is named once it has led for the whole hold" *
          doctest::test_suite("main")) {
    Detector detector{kTuning};
    uint32_t now_ms = 1000;
    listen(detector, now_ms, kTuning.lead_hold_ms, -20, -60);
    CHECK(named(detector) == 0);
    listen(detector, now_ms, kStepMs, -20, -60);
    CHECK(named(detector) == kOwn);
}

// A neighbour inside the lead is a guess, and the detector makes none.
TEST_CASE("aboard: two emitters inside the lead of each other name neither") {
    Detector detector{kTuning};
    uint32_t now_ms = 1000;
    listen(detector, now_ms, 3 * kTuning.lead_hold_ms, -20, -25);
    CHECK(named(detector) == 0);
}

TEST_CASE("aboard: nothing under the floor is named, however clear its lead") {
    Detector detector{kTuning};
    uint32_t now_ms = 1000;
    listen(detector, now_ms, 3 * kTuning.lead_hold_ms, -55, 0);
    CHECK(named(detector) == 0);
}

TEST_CASE("aboard: an emitter gone quiet is forgotten, and a new leader starts its hold again") {
    Detector detector{kTuning};
    uint32_t now_ms = 1000;
    listen(detector, now_ms, 2 * kTuning.lead_hold_ms, -20, -60);
    REQUIRE(named(detector) == kOwn);

    listen(detector, now_ms, kTuning.emitter_max_age_ms + kStepMs, 0, -40);
    CHECK(named(detector) == 0);
    listen(detector, now_ms, kTuning.lead_hold_ms, 0, -40);
    CHECK(named(detector) == kNeighbour);
}

TEST_CASE("aboard: a frame with no level writes only to an emitter already tracked") {
    Detector detector{kTuning};
    CHECK(detector.hear(kOwn, false, 0, 1000) == nullptr);
    Tag* tag = detector.hear(kOwn, true, -20, 1000);
    REQUIRE(tag != nullptr);
    tag->frames++;
    Tag* again = detector.hear(kOwn, false, 0, 1500);
    REQUIRE(again == tag);
    CHECK(again->frames == 1);
}

// A full table gives up an emitter gone quiet first, then the faintest, never the leader.
TEST_CASE("aboard: a full table admits a new emitter in place of the faintest") {
    Detector detector{kTuning};
    uint32_t now_ms = 1000;
    listen(detector, now_ms, 2 * kTuning.lead_hold_ms, -20, -60);
    REQUIRE(named(detector) == kOwn);
    (void)detector.hear(3, true, -65, now_ms);
    detector.tick(now_ms);
    CHECK(named(detector) == kOwn);
}

TEST_CASE("aboard: an emitter aboard names a FLARM ID only under table 6") {
    CHECK(flarm_id_of(AboardHeard{0xDD1234, adsl::kAddressTableFlarm}) == 0xDD1234u);
    CHECK_FALSE(flarm_id_of(AboardHeard{0x3A4B5C, adsl::kAddressTableIcao}));
    CHECK_FALSE(flarm_id_of(AboardHeard{0x00ABCD, adsl::kAddressTableRandom}));
    CHECK_FALSE(flarm_id_of(std::nullopt));
}

// Our own FLARM, heard on ALP-TAS, on ADS-L or through a ground station, is never traffic.
TEST_CASE("traffic: the FLARM ID set is refused under table 6 alone, and its slot released" *
          doctest::test_suite("main")) {
    TrafficTable table;
    table.set_own_reference(flying(30, 0));
    REQUIRE(table.update(flarm(0xDD1234, adsl::kAddressTableFlarm), 100) >= 0);
    REQUIRE(table.update(flarm(0xDD1234, adsl::kAddressTableIcao), 100) >= 0);
    REQUIRE(table.count() == 2);

    table.set_flarm_id(0xDD1234);
    CHECK(table.find(adsl::kAddressTableFlarm, 0xDD1234) < 0);
    CHECK(table.find(adsl::kAddressTableIcao, 0xDD1234) >= 0);
    CHECK(table.update(flarm(0xDD1234, adsl::kAddressTableFlarm), 101) < 0);
    CHECK(table.update(flarm(0xDD1235, adsl::kAddressTableFlarm), 101) >= 0);

    table.set_flarm_id(0);
    CHECK(table.update(flarm(0xDD1234, adsl::kAddressTableFlarm), 102) >= 0);
}
