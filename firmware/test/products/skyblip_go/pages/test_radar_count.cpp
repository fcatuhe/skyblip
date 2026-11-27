// The radar's traffic count: whatever can fill the table counts, with no PPS lock or fix asked.
#include "core/timing/slot.h"
#include "test/support/glass_text.h"
#include "test/support/screen_rig.h"

namespace {

constexpr uint64_t kLastEdgeUs = 12'000'000;

bool count_reads(ScreenRig& rig, const char* text) {
    return reads_in(rig.chip.framebuffer(), text, 170, 170, 200, 200, 3);
}

void with_radio(ScreenRig& rig) {
    rig.roles.capabilities = ports::Capability::Display | ports::Capability::Rf;
    rig.state.clock.utc_valid = true;
}

void held(ScreenRig& rig) {
    rig.state.clock.pps_locked = false;
    rig.state.clock.since_edge_ms = 10'000;
    rig.state.clock.pps_edge_us = kLastEdgeUs;
}

void relayed_adsb(ScreenRig& rig, uint32_t address, int32_t north_m) {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.address_mapping_table = 5;
    observation.position_valid = true;
    observation.latitude_1e7 =
        rig.state.own.latitude_1e7 + static_cast<int32_t>(int64_t(north_m) * 1000000 / 11132);
    observation.longitude_1e7 = rig.state.own.longitude_1e7;
    observation.altitude_m = to_metres(Millimetres(rig.state.own.altitude_hae_mm)).value;
    observation.altitude_valid = true;
    observation.received.at_s = 1;
    observation.at_ms = 1000;
    observation.source = model::Source::Adsb;
    REQUIRE(rig.state.traffic.update(observation, 1) >= 0);
}

}  // namespace

TEST_CASE("radar count: a locked radar counts the aircraft it plots") {
    ScreenRig rig;
    with_radio(rig);
    rig.state.clock.pps_locked = true;
    rig.threat(traffic::Level::None, 2000, 0, 1000);
    rig.run_seconds(3);
    CHECK(count_reads(rig, "1"));
}

// core/timing/README.md: the PPS holdover keeps the dwells, receive only.
TEST_CASE("radar count: the radar keeps counting the aircraft it plots through the PPS holdover") {
    ScreenRig rig;
    with_radio(rig);
    held(rig);
    REQUIRE(timing::in_pps_holdover(rig.state.clock));
    rig.threat(traffic::Level::None, 2000, 0, 1000);
    rig.run_seconds(3);
    CHECK(rig.chip.framebuffer().count_black() > 0);
    CHECK_FALSE(count_reads(rig, "-"));
    CHECK(count_reads(rig, "1"));
}

// The Go's fix and UTC place Bravo's ADS-B, and the Go's pulse plays no part in it.
TEST_CASE("radar count: a live Bravo's relayed ADS-B is counted without the Go's pulse") {
    ScreenRig rig;
    with_radio(rig);
    rig.state.clock.pps_locked = false;
    rig.state.clock.pps_edge_us = 0;
    rig.state.bravo = comms::BravoState::Live;
    relayed_adsb(rig, 0x3C1234, 3000);
    relayed_adsb(rig, 0x3C5678, -4000);
    rig.run_seconds(3);
    CHECK_FALSE(count_reads(rig, "-"));
    CHECK(count_reads(rig, "2"));
}

// An ADS-L aircraft heard with no fix of ours is still heard, and the plot is what waits for a fix.
TEST_CASE("radar count: a Go with no fix counts the aircraft its radio heard") {
    ScreenRig rig;
    with_radio(rig);
    rig.state.clock.pps_locked = false;
    rig.threat(traffic::Level::None, 2000, 0, 1000);
    rig.state.own.fix_valid = false;
    rig.run_seconds(3);
    CHECK(count_reads(rig, "1"));
}

TEST_CASE("radar count: a Go with no radio dashes, and a relaying Bravo lifts it") {
    ScreenRig rig;
    rig.state.clock.utc_valid = true;
    rig.run_seconds(3);
    CHECK(count_reads(rig, "-"));

    rig.state.bravo = comms::BravoState::CellLow;
    relayed_adsb(rig, 0x3C1234, 3000);
    rig.run_seconds(2);
    CHECK(count_reads(rig, "1"));
}

// What the Go transmits is NO TX's to say, so it never hides what the Go's radio holds.
TEST_CASE("radar count: nearby's Go row counts its traffic while the Go does not transmit") {
    ScreenRig rig;
    with_radio(rig);
    held(rig);
    rig.threat(traffic::Level::None, 2000, 0, 1000);
    rig.show(go::Page::Nearby);
    rig.run_seconds(3);
    REQUIRE_FALSE(rig.state.transmit.on_air());
    CHECK_FALSE(reads_in(rig.chip.framebuffer(), "TX OFF", 0, 0, 200, 20, 2));
    CHECK(reads_in(rig.chip.framebuffer(), "TFC", 0, 0, 200, 20));
}
