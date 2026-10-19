// ADS-L 4 SRD-860 issue 2 G.2 to G.5: the Status, Traffic Uplink, FIS-B and Remote ID payloads.
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/protocol/adsl_uplink.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"

using namespace skyblip;

namespace {
model::AircraftObs target(uint32_t address) {
    model::AircraftObs t{};
    t.address = address;
    t.address_mapping_table = 6;
    t.aircraft_cat = 1;
    t.flight_state = 2;
    t.lat_1e7 = 481234500;
    t.lon_1e7 = 81234500;
    t.alt_m = 1200;
    t.speed_q = 160;
    t.speed_valid = true;
    t.position_valid = true;
    return t;
}
}  // namespace

// TODO: fc 18sep26 no Status payload is transmitted, so G.2.1 to G.2.11 have nothing to encode
TEST_CASE("ADS-L.4.SRD860.G.2: a Status payload declares this device's bands, systems and version" *
          doctest::skip()) {
    FAIL("the Status payload is not implemented: no type 3 is ever transmitted");
}

// TODO: fc 18sep26 no Status payload, so none of its fields is encoded: G.2.1 to G.2.11 below
TEST_CASE("ADS-L.4.SRD860.G.2.1: the Status payload names the highest version this device reads" *
          doctest::skip()) {
    FAIL("no Status payload: the Max. Protocol Version field is never transmitted");
}

// TODO: fc 18sep26 the M-band dwell is one channel at a time, which is a Partial claim to declare
TEST_CASE("ADS-L.4.SRD860.G.2.2: the Status payload declares the M-band dwell as a receive class" *
          doctest::skip()) {
    FAIL("no Status payload: the M-band receive capability is never declared");
}

// TODO: fc 18sep26 nothing receives O-band LDR, which is the None class, still undeclared
TEST_CASE("ADS-L.4.SRD860.G.2.3: the Status payload declares the O-band LDR receive class" *
          doctest::skip()) {
    FAIL("no Status payload: the O-band LDR receive capability is never declared");
}

// TODO: fc 18sep26 the O-band HDR dwell is 190 ms of the second, and nothing says so on air
TEST_CASE("ADS-L.4.SRD860.G.2.4: the Status payload declares the O-band HDR receive class" *
          doctest::skip()) {
    FAIL("no Status payload: the O-band HDR receive capability is never declared");
}

// TODO: fc 18sep26 no ADS-L 4 Mobile client on this device, which is the None value, undeclared
TEST_CASE("ADS-L.4.SRD860.G.2.5: the Status payload declares the ADS-L 4 Mobile state" *
          doctest::skip()) {
    FAIL("no Status payload: the ADS-L 4 Mobile state is never declared");
}

// TODO: fc 18sep26 this device receives the FLARM-generation wire, which is bit 0 of the bitmap
TEST_CASE("ADS-L.4.SRD860.G.2.6: the Status payload declares which other systems are received" *
          doctest::skip()) {
    FAIL("no Status payload: the e-conspicuity receive bitmap is never declared");
}

// TODO: fc 18sep26 nothing but ADS-L is transmitted here, so the bitmap would be empty and honest
TEST_CASE("ADS-L.4.SRD860.G.2.7: the Status payload declares which other systems are transmitted" *
          doctest::skip()) {
    FAIL("no Status payload: the e-conspicuity transmit bitmap is never declared");
}

// TODO: fc 18sep26 no transponder is wired to this device, and no setting records one
TEST_CASE("ADS-L.4.SRD860.G.2.8: the Status payload declares the transponder fitted" *
          doctest::skip()) {
    FAIL("no Status payload: the XPDR capability is never declared");
}

// TODO: fc 18sep26 this device does process Traffic Uplink, and no peer is told
TEST_CASE("ADS-L.4.SRD860.G.2.9: the Status payload declares that Traffic Uplink is processed" *
          doctest::skip()) {
    FAIL("no Status payload: the Traffic Uplink client flag is never set");
}

// TODO: fc 18sep26 no FIS-B is processed, which is the zero flag, still undeclared
TEST_CASE("ADS-L.4.SRD860.G.2.10: the Status payload declares whether FIS-B is processed" *
          doctest::skip()) {
    FAIL("no Status payload: the FIS-B client flag is never set");
}

// TODO: fc 18sep26 a Status payload every 20 s is a slot in the transmit schedule, unbuilt
TEST_CASE("ADS-L.4.SRD860.G.2.11: a Status payload goes out at least every 20 seconds" *
          doctest::skip()) {
    FAIL("no Status payload: nothing schedules one");
}

// The uplink carried here is the ground station's own, on the channel and in the slot G.3 names.
TEST_CASE(
    "ADS-L.4.SRD860.G.3: the Traffic Uplink rides the O-band HDR channel in the Uplink slot") {
    CHECK(timing::kObandHz == 869525000u);
    CHECK(protocol::kUplinkChipRateBps == 200000u);
    CHECK(timing::kUplinkRxStart >= 200);
    CHECK(timing::kUplinkRxEnd <= 450);
    // A whole burst fits the slot it is transmitted in, with the dwell's guard either side.
    CHECK(timing::kGroundEmitStart + static_cast<int>(protocol::kUplinkBurstUs / 1000) <=
          timing::kGroundEmitEnd);
}

// A ground station reaches every aircraft in range, so what it relays is checked, not trusted.
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed target that is not an aircraft is refused, not drawn") {
    protocol::AdslUplink uplink;
    model::AircraftObs targets[4] = {target(0x300001), target(0), target(0x300003),
                                     target(0x300004)};
    targets[3].lat_1e7 = 1800000000;

    uint8_t frame[protocol::AdslUplink::kFrameBytes] = {0};
    REQUIRE(uplink.encode(targets, 4, 0, frame) == Status::Ok);

    model::AircraftObs out[4]{};
    protocol::AdslUplink::DecodeStats stats{};
    REQUIRE(uplink.decode(frame, out, 4, stats) == Status::Ok);
    CHECK(stats.targets == 2);
    CHECK(stats.rejected == 2);
    for (int i = 0; i < stats.targets; i++) CHECK(out[i].source == model::Source::AdslUplink);
}

// TODO: fc 18sep26 the uplink frame is a house format: 16-byte records, no verbatim payload
TEST_CASE(
    "ADS-L.4.SRD860.G.3: an uplink record is a 30-bit address and the Traffic payload, "
    "verbatim" *
    doctest::skip()) {
    const int record_bits = 30 + 2 + 120;
    CHECK(record_bits == 152);
    CHECK(protocol::AdslUplink::kRecordBytes * 8 == record_bits);
    CHECK(protocol::AdslUplink::kMaxTargets == 10);
}

// TODO: fc 18sep26 an uplink record carries no timestamp, so its age cannot be checked at all
TEST_CASE("ADS-L.4.SRD860.G.3: a relayed Traffic payload older than 10 seconds is not forwarded" *
          doctest::skip()) {
    FAIL("no timestamp travels with a relayed target, so nothing here can refuse a stale one");
}

// TODO: fc 18sep26 no FIS-B: DO-358B ground uplink messages are neither received nor decoded
TEST_CASE("ADS-L.4.SRD860.G.4: a FIS-B Uplink payload carries a DO-358B ground uplink message" *
          doctest::skip()) {
    FAIL("the FIS-B payload is not implemented: type 5 is dropped with every other unknown type");
}

// TODO: fc 18sep26 no Remote Identification: a drone payload this aircraft device never sends
TEST_CASE("ADS-L.4.SRD860.G.5: a Remote Identification payload carries an EN 4709-002 block" *
          doctest::skip()) {
    FAIL("the Remote Identification payload is not implemented: type 6 is dropped as unknown");
}
