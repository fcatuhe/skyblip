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
    gnss::NmeaParser p;
    p.parse_line(rmc, static_cast<int>(std::strlen(rmc)));
    p.parse_line(gga, static_cast<int>(std::strlen(gga)));
    p.parse_line(gsa, static_cast<int>(std::strlen(gsa)));
    const gnss::GnssSolution& f = p.solution();
    model::Ownship o{};
    o.fix_valid = f.fix_valid;
    o.utc_valid = f.utc_valid;
    o.pps_locked = true;
    o.lat_1e7 = f.lat_1e7;
    o.lon_1e7 = f.lon_1e7;
    o.alt_hae_mm = f.alt_hae_mm;
    o.speed_mm_s = f.speed_mm_s;
    o.track_cdeg = f.track_cdeg;
    o.vdop_e2 = f.vdop_e2;
    o.utc_s = f.utc_s;
    o.sats = f.sats;
    o.flight_state = 2;
    return o;
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
    intruder_state.lat_1e7 = own.lat_1e7 + static_cast<int32_t>((int64_t)800 * 1000000 / 11132);
    intruder_state.track_cdeg = 18000;  // due south
    intruder_state.utc_s = own.utc_s;
    protocol::AdslPacket tx;
    protocol::from_own(tx, intruder_state, 0xC5D804, /*table=*/6, /*cat=*/4);
    tx.scramble();
    tx.set_crc();

    // 3) go over the air: manchester encode 24 data bytes, inject light BER,
    //    manchester decode, CRC-correct, verify, descramble.
    uint8_t coded[48];
    fec::manchester_encode(tx.Data, protocol::AdslPacket::kDataBytes, coded);
    RfChannel chan(12345);
    chan.apply_ber(coded, sizeof(coded), 0.002);  // ~0.2% chip errors

    protocol::AdslPacket rx = tx;  // start from a copy; overwrite the data region
    uint8_t err[protocol::AdslPacket::kDataBytes];
    fec::manchester_decode(coded, protocol::AdslPacket::kDataBytes, rx.Data, err);
    rx.correct(err, 6);
    REQUIRE(rx.check_crc() == 0);  // recovered a valid packet
    rx.descramble();

    // 4) decode -> observation -> table
    model::AircraftObservation observation;
    REQUIRE(protocol::to_observation(rx, events::Stamp{own.utc_s, 500, true}, -80,
                                     model::Source::AdslDirect, observation));
    CHECK(observation.address == 0xC5D804u);
    CHECK(observation.alt_valid);

    traffic::TrafficTable table;
    int idx = table.update(observation, own.utc_s);
    REQUIRE(idx >= 0);
    CHECK(table.count() == 1);

    // 5) alarm: an aircraft 800 m away and co-altitude is an advisory, and the
    //    closure beside it comes off the relative velocity vector.
    traffic::AlarmAssessment a = traffic::assess(own, observation, own.fix_at_ms);
    CHECK(a.valid);
    CHECK(a.rel_dist_m > 700);
    CHECK(a.rel_dist_m < 900);
    CHECK(a.rel_alt_m == 0);
    CHECK(a.closing_mps > 40);
    CHECK(a.level == traffic::Level::Advisory);

    // The same aircraft flying the way we are: the gap is not closing, and it is
    // an advisory all the same, because it is there.
    model::AircraftObservation chase = observation;
    chase.track_c9 = to_cordic9(CentiDegrees(own.track_cdeg)).v;
    const traffic::AlarmAssessment following = traffic::assess(own, chase, own.fix_at_ms);
    CHECK(following.closing_mps <= 0);
    CHECK(following.level == traffic::Level::Advisory);

    table.at(idx)->alarm_level = a.level;

    // 6) NMEA out to the EFB link
    platform::host::Link efb;
    efb.raise_link(1);
    char buf[128];
    int n = protocol::format_pflaa(buf, sizeof(buf), own, observation, traffic::to_number(a.level),
                                   nullptr);
    REQUIRE(n > 0);
    REQUIRE(efb.send(events::Endpoint::Nmea, ConstByteSpan(reinterpret_cast<uint8_t*>(buf), n)) ==
            Status::Ok);
    n = protocol::format_pflau(buf, sizeof(buf), own, true, table.count(), &observation,
                               traffic::to_number(a.level), a.rel_bearing_deg, a.rel_alt_m,
                               a.rel_dist_m);
    REQUIRE(efb.send(events::Endpoint::Nmea, ConstByteSpan(reinterpret_cast<uint8_t*>(buf), n)) ==
            Status::Ok);
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
    up.lat_1e7 = own.lat_1e7 + 50000;
    up.lon_1e7 = own.lon_1e7;
    up.alt_m = 1100;
    up.source = model::Source::AdslUplink;
    up.received.at_s = own.utc_s;

    // Encode+decode through the real uplink codec (the anti-drift lock).
    protocol::AdslUplink codec;
    uint8_t frame[protocol::AdslUplink::kFrameBytes];
    REQUIRE(codec.encode(&up, 1, 0, frame) == Status::Ok);
    model::AircraftObservation decoded[4];
    protocol::AdslUplink::DecodeStats st;
    REQUIRE(codec.decode(frame, decoded, 4, st) == Status::Ok);
    REQUIRE(st.targets == 1);
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
