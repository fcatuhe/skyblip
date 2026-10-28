// End-to-end host scenario (no hardware): the full skyBlip Go air-side pipeline.
//   GNSS NMEA -> Ownship
//   ADS-L direct RX (scramble+crc+manchester over a BER channel) -> observation -> table
//   alarm assessment -> $PFLAA/$PFLAU to the EFB link
//   ADS-L uplink RX -> observation -> table dedup/merge
// This is the "run all of core/ headless" harness and doubles as
// the composition sanity check that everything fits together with no #ifdef.
#include <cstring>
#include <string>

#include "core/events/link.h"
#include "core/fec/manchester.h"
#include "core/gnss/nmea.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/traffic/table.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "test/support/rf_channel.h"

using namespace skyblip;

namespace {
constexpr const char* kRmc = "$GPRMC,120000,A,4807.000,N,00800.000,E,050.0,000.0,230324,,*1C";
constexpr const char* kGga = "$GPGGA,120000,4807.000,N,00800.000,E,1,09,0.8,1000.0,M,47,M,,*6F";
constexpr const char* kGsa3d = "$GPGSA,A,3,04,05,09,12,15,17,20,24,25,,,,1.4,0.8,1.1*35";

model::Ownship own_from_gnss(const char* rmc, const char* gga, const char* gsa) {
    gnss::NmeaParser parser;
    parser.parse_line(rmc, static_cast<int>(std::strlen(rmc)));
    parser.parse_line(gga, static_cast<int>(std::strlen(gga)));
    parser.parse_line(gsa, static_cast<int>(std::strlen(gsa)));
    const gnss::GnssSolution& solution = parser.solution();
    model::Ownship own{};
    own.fix_valid = solution.fix_valid;
    own.utc_valid = solution.utc_valid;
    own.pps_locked = true;
    own.latitude_1e7 = solution.latitude_1e7;
    own.longitude_1e7 = solution.longitude_1e7;
    own.altitude_hae_mm = solution.altitude_hae_mm;
    own.speed_mm_s = solution.speed_mm_s;
    own.track_cdeg = solution.track_cdeg;
    own.vdop_e2 = solution.vdop_e2;
    own.utc_s = solution.utc_s;
    own.satellites = solution.satellites;
    own.flight_state = 2;
    return own;
}
}  // namespace

TEST_CASE("scenario: GNSS -> own, direct ADS-L RX over BER channel -> alarm -> NMEA") {
    // 1) own-ship from GNSS
    model::Ownship own = own_from_gnss(kRmc, kGga, kGsa3d);
    REQUIRE(own.fix_valid);

    // 2) an intruder ~800 m north, co-altitude, head-on. Own-ship is tracking
    //    north at 50 kt; the intruder tracks south at the same speed, so the two
    //    close at about 51 m/s and meet in under the urgent time to impact. It
    //    used to be a copy of own-ship displaced north, which is a chase at a
    //    constant 800 m and alarms for the range alone - the assertion below
    //    says which of the two this fixture means.
    model::Ownship intruder_state = own;
    intruder_state.latitude_1e7 =
        own.latitude_1e7 + static_cast<int32_t>((int64_t)800 * 1000000 / 11132);
    intruder_state.track_cdeg = 18000;  // due south
    intruder_state.utc_s = own.utc_s;
    protocol::AdslPacket tx;
    protocol::from_own(tx, intruder_state, 0xC5D804, /*table=*/6, /*cat=*/4);
    tx.scramble();
    tx.set_crc();

    // 3) go over the air: manchester encode 24 data bytes, inject light BER,
    //    manchester decode, CRC-correct, verify, descramble.
    uint8_t coded[48];
    fec::manchester_encode(tx.data, protocol::AdslPacket::kDataBytes, coded);
    RfChannel channel(12345);
    channel.apply_bit_error_rate(coded, sizeof(coded), 0.002);  // ~0.2% chip errors

    protocol::AdslPacket rx = tx;  // start from a copy; overwrite the data region
    uint8_t error[protocol::AdslPacket::kDataBytes];
    fec::manchester_decode(coded, protocol::AdslPacket::kDataBytes, rx.data, error);
    rx.correct(error, 6);
    REQUIRE(rx.check_crc() == 0);  // recovered a valid packet
    rx.descramble();

    // 4) decode -> observation -> table
    model::AircraftObservation observation;
    REQUIRE(protocol::to_observation(rx, events::Stamp{own.utc_s, 500, true}, -80,
                                     model::Source::AdslDirect, observation));
    CHECK(observation.address == 0xC5D804u);
    CHECK(observation.altitude_valid);

    traffic::TrafficTable table;
    int index = table.update(observation, own.utc_s);
    REQUIRE(index >= 0);
    CHECK(table.count() == 1);

    // 5) alarm: an aircraft 800 m away and co-altitude is an advisory, and the
    //    closure beside it comes off the relative velocity vector.
    traffic::AlarmAssessment assessment = traffic::assess(own, observation, own.fix_at_ms);
    CHECK(assessment.valid);
    CHECK(assessment.relative_distance_m > 700);
    CHECK(assessment.relative_distance_m < 900);
    CHECK(assessment.relative_altitude_m == 0);
    CHECK(assessment.closing_mps > 40);
    CHECK(assessment.level == traffic::Level::Advisory);

    // The same aircraft flying the way we are: the gap is not closing, and it is
    // an advisory all the same, because it is there.
    model::AircraftObservation chase = observation;
    chase.track_c9 = to_cordic9(CentiDegrees(own.track_cdeg)).value;
    const traffic::AlarmAssessment following = traffic::assess(own, chase, own.fix_at_ms);
    CHECK(following.closing_mps <= 0);
    CHECK(following.level == traffic::Level::Advisory);

    table.at(index)->alarm_level = assessment.level;

    // 6) NMEA out to the EFB link
    platform::host::Link efb;
    efb.raise_link(1);
    char buffer[128];
    int written = protocol::format_pflaa(buffer, sizeof(buffer), own, observation,
                                         traffic::to_number(assessment.level), nullptr);
    REQUIRE(written > 0);
    REQUIRE(efb.send(events::Endpoint::Nmea,
                     ConstByteSpan(reinterpret_cast<uint8_t*>(buffer), written)) == Status::Ok);
    written = protocol::format_pflau(
        buffer, sizeof(buffer), own, true, table.count(), &observation,
        traffic::to_number(assessment.level), assessment.relative_bearing_deg,
        assessment.relative_altitude_m, assessment.relative_distance_m);
    REQUIRE(efb.send(events::Endpoint::Nmea,
                     ConstByteSpan(reinterpret_cast<uint8_t*>(buffer), written)) == Status::Ok);
    CHECK(efb.count_on(events::Endpoint::Nmea) == 2);
    CHECK(efb.sent[0].bytes.find("C5D804") != std::string::npos);
}

TEST_CASE("scenario: uplink RX merges with direct RX (dedup, prefer direct)") {
    model::Ownship own = own_from_gnss(kRmc, kGga, kGsa3d);

    traffic::TrafficTable table;

    // First seen only via uplink (relayed from a ground station).
    model::AircraftObservation up{};
    up.address = 0x3FBEEF;
    up.address_mapping_table = 5;  // ICAO (from ADS-B via uplink)
    up.position_valid = true;
    up.latitude_1e7 = own.latitude_1e7 + 50000;
    up.longitude_1e7 = own.longitude_1e7;
    up.altitude_m = 1100;
    up.source = model::Source::AdslUplink;
    up.received.at_s = own.utc_s;

    // Encode+decode through the real uplink codec (the anti-drift lock).
    protocol::AdslUplink codec;
    uint8_t frame[protocol::AdslUplink::kFrameBytes];
    REQUIRE(codec.encode(&up, 1, 0, frame) == Status::Ok);
    model::AircraftObservation decoded[4];
    protocol::AdslUplink::DecodeStatistics statistics;
    REQUIRE(codec.decode(frame, decoded, 4, statistics) == Status::Ok);
    REQUIRE(statistics.targets == 1);
    table.update(decoded[0], own.utc_s);
    CHECK(table.at(table.find(5, 0x3FBEEF))->observation.source == model::Source::AdslUplink);

    // Later the same aircraft is heard directly on M-band -> prefer direct.
    model::AircraftObservation direct = decoded[0];
    direct.source = model::Source::AdslDirect;
    direct.received.at_s = own.utc_s + 1;
    table.update(direct, own.utc_s + 1);
    CHECK(table.count() == 1);  // merged, not duplicated
    CHECK(table.at(table.find(5, 0x3FBEEF))->observation.source == model::Source::AdslDirect);
}
