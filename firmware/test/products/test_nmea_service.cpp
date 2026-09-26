// What a pilot's tablet actually receives from a running skyBlip Go.
//
// The tree formatted $PFLAA/$PFLAU/$PGRMZ for months, tested the formatters
// hard, routed the endpoint, advertised the characteristic - and never sent a
// byte, because no service called any of it. So every case here asserts on the
// bytes that left the link, reassembled the way an EFB reassembles them: the
// frames are a stream, and a sentence is what lies between two CRLFs.
//
// Interoperability, in one place, because getting it wrong is worse than
// shipping nothing:
//   $PFLAA - the traffic sentence every app reads. SkyDemon, XCSoar and SDVFR
//            Next all draw from it; SkyDemon accepts only IDType 1 or 2 (see
//            oss/SoftRF-moshe-braner/.../libraries/OGN/ads-l.h:657).
//   $PFLAU - system state and nearest threat. XCSoar sounds its own alarms off
//            it (oss/SoftRF-moshe-braner/.../src/TrafficHelper.cpp:1913); the
//            open-source GATAS suppresses it for SkyDemon
//            (oss/openace/.../dataport.cpp:31). It is one sentence a second and
//            it is the one this service refuses to drop.
//   $PGRMZ - barometric altitude, read as PRESSURE altitude by the app, which
//            then applies its own QNH. Hence the datum asserted below. Field 3
//            is the fix dimension (both SoftRF forks: '3' with a fix, '1'
//            without), not the constant it used to be.
//   $GPRMC/$GPGGA - ownship's own absolute position, for the EFB with no GNSS
//            of its own: a panel-mounted tablet with no sky view. Sent only
//            with a real fix and a real UTC time, on the same cadence as
//            everything above.
#include <string>
#include <vector>

#include "core/events/link.h"
#include "core/events/rf.h"
#include "core/flight/atmosphere.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/model.h"
#include "hardware/platform/host/clock.h"
#include "hardware/platform/host/link.h"
#include "ports/null.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// Every byte the device put on the NMEA endpoint, in order: an EFB sees one
// stream, not a sequence of notifications.
std::string stream(Rig& rig) {
    std::string all;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == events::Endpoint::Nmea) all += frame.bytes;
    return all;
}

std::vector<std::string> sentences(Rig& rig) {
    std::vector<std::string> out;
    const std::string all = stream(rig);
    size_t at = 0;
    while (true) {
        const size_t end = all.find("\r\n", at);
        if (end == std::string::npos) break;
        out.push_back(all.substr(at, end - at));
        at = end + 2;
    }
    return out;
}

bool checksum_ok(const std::string& sentence) {
    const size_t star = sentence.find('*');
    if (star == std::string::npos || sentence.size() < star + 3) return false;
    uint8_t sum = 0;
    for (size_t i = 1; i < star; i++) sum ^= static_cast<uint8_t>(sentence[i]);
    const std::string hex = sentence.substr(star + 1, 2);
    return static_cast<uint8_t>(std::stoi(hex, nullptr, 16)) == sum;
}

std::vector<std::string> fields(const std::string& sentence) {
    std::vector<std::string> out;
    const std::string body = sentence.substr(0, sentence.find('*'));
    size_t at = 0;
    while (true) {
        const size_t comma = body.find(',', at);
        if (comma == std::string::npos) {
            out.push_back(body.substr(at));
            return out;
        }
        out.push_back(body.substr(at, comma - at));
        at = comma + 1;
    }
}

int count_of(Rig& rig, const char* kind) {
    int n = 0;
    for (const std::string& s : sentences(rig))
        if (s.rfind(kind, 0) == 0) n++;
    return n;
}

// A burst on air, from a transmitter that is where the case says it is. It goes
// in as chips through the same sync-window strip the SX1262 does, so the
// product's own decoder is what turns it into a target: nothing here writes to
// the traffic table.
void hear(Rig& rig, uint32_t addr, int32_t north_m, int32_t east_m, int32_t up_m,
          uint16_t track_c9 = 256) {
    model::OwnState transmitter = rig.state().own;
    transmitter.lat_1e7 += static_cast<int32_t>(static_cast<int64_t>(north_m) * 1000000 / 11132);
    transmitter.lon_1e7 += static_cast<int32_t>(static_cast<int64_t>(east_m) * 1000000 / 7460);
    transmitter.alt_mm += up_m * 1000;
    transmitter.track_cdeg = to_centi_degrees(Cordic9(track_c9)).v;
    transmitter.speed_mm_s = 40000;
    transmitter.vdop_e2 = 150;

    protocol::AdslPacket packet;
    protocol::from_own(packet, transmitter, addr, /*addr_table=*/6, /*aircraft_cat=*/4);
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_len = protocol::encode_mband(protocol::kAdslSyncWord, packet.Data,
                                                   protocol::kAdslFrameBytes, chips);

    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.rssi_dbm = -80;
    event.len = models::Sx1262::deliver_after_sync(chips, static_cast<uint8_t>(chip_len),
                                                   protocol::kSharedSync, protocol::kSharedSyncBits,
                                                   event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

// The same aircraft naming itself, one Type 66 burst (core/protocol/README.md).
void hear_callsign(Rig& rig, uint32_t addr, const char* callsign) {
    protocol::AdslPacket packet;
    protocol::from_own_callsign(packet, addr, /*addr_table=*/6, callsign);
    packet.scramble();
    packet.set_crc();

    uint8_t chips[protocol::kTxChipBytes];
    const size_t chip_len = protocol::encode_mband(protocol::kAdslSyncWord, packet.Data,
                                                   protocol::kAdslFrameBytes, chips);

    events::RfEvent event{};
    event.type = events::RfEventType::RxDone;
    event.rssi_dbm = -80;
    event.len = models::Sx1262::deliver_after_sync(chips, static_cast<uint8_t>(chip_len),
                                                   protocol::kSharedSync, protocol::kSharedSyncBits,
                                                   event.data.data(), protocol::kRxChipBytes);
    rig.product.bus().rf.push(event);
}

// Airborne, timed and moving: everything below needs a fix, because a relative
// position has no meaning without one.
void fly(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 25000, 900); }

// Parked with the receiver running, which is where a pilot pairs a tablet.
void park(Rig& rig, uint32_t& t, uint32_t seconds) { rig.seconds(t, seconds, 200, 900); }

std::string last_of(Rig& rig, const char* kind) {
    std::string found;
    for (const std::string& s : sentences(rig))
        if (s.rfind(kind, 0) == 0) found = s;
    return found;
}

}  // namespace

// THE GUARD. Delete the service from the product's list, or its call to
// ports::Link::send, and this is the case that goes red. It is written the way a
// pilot experiences the feature: pair a tablet, see traffic; walk away, see it
// stop.
TEST_CASE("nmea: a tablet that pairs starts hearing sentences, and they stop when it leaves") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);

    // Nobody is listening, so nothing is said - and nothing was formatted to say.
    CHECK(rig.platform.link().count_on(events::Endpoint::Nmea) == 0);

    rig.raise_link();
    fly(rig, t, 3);
    REQUIRE(rig.link_up());
    const std::vector<std::string> heard = sentences(rig);
    REQUIRE(heard.size() >= 3);
    for (const std::string& s : heard) CHECK(checksum_ok(s));
    // The status sentence is the heartbeat: one per second, with or without
    // traffic, which is how an app knows the device is alive and has a fix.
    CHECK(count_of(rig, "$PFLAU") >= 3);

    rig.platform.link().clear();
    rig.drop_link();
    fly(rig, t, 3);
    CHECK(rig.platform.link().count_on(events::Endpoint::Nmea) == 0);
}

TEST_CASE("nmea: an aircraft heard over the air becomes a $PFLAA a tablet can parse") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);
    rig.platform.link().clear();

    // 1200 m north, 800 m east, 150 m above.
    hear(rig, 0xC5D804, 1200, 800, 150);
    fly(rig, t, 2);

    std::string traffic;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PFLAA,", 0) == 0) traffic = s;
    REQUIRE_FALSE(traffic.empty());
    CHECK(checksum_ok(traffic));

    const std::vector<std::string> f = fields(traffic);
    REQUIRE(f.size() >= 12);
    CHECK(std::stoi(f[2]) > 900);  // relative north, metres
    CHECK(std::stoi(f[2]) < 1500);
    CHECK(std::stoi(f[3]) > 500);  // relative east
    CHECK(std::stoi(f[3]) < 1100);
    CHECK(std::stoi(f[4]) > 100);  // relative vertical, + is above
    // IDType 2 (the FLARM address table), which with 1 is all SkyDemon accepts,
    // then the 24-bit address as six hex digits.
    CHECK(f[5] == "2");
    CHECK(f[6] == "C5D804");
    CHECK(f[11] == "1");  // ALP-TAS aircraft type: glider
}

TEST_CASE("nmea: a target that named itself reaches the tablet with the name on its ID") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);

    hear(rig, 0xC5D804, 1200, 800, 150);
    hear_callsign(rig, 0xC5D804, "D-KXYZ");
    rig.platform.link().clear();
    hear(rig, 0xC5D804, 1200, 800, 150);
    fly(rig, t, 2);

    const std::vector<std::string> f = fields(last_of(rig, "$PFLAA,"));
    REQUIRE(f.size() >= 12);
    CHECK(f[6] == "C5D804!D-KXYZ");
}

// FTD-012's GPS field is 1 for a 3D fix on the ground, 2 for one moving, and XCSoar names 1 GPS_2D.
TEST_CASE("nmea: PFLAU says GPS 1 on the ground, which XCSoar draws as a 2D fix") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    park(rig, t, 3);
    rig.raise_link();
    park(rig, t, 3);
    REQUIRE(rig.state().own.fix_valid);
    REQUIRE(flight::state_from(rig.state().own.flight_state) == flight::FlightState::OnGround);
    CHECK(fields(last_of(rig, "$PFLAU"))[3] == "1");

    // The fix dimension is elsewhere and unaffected: $PGRMZ still says 3D.
    CHECK(fields(last_of(rig, "$PGRMZ"))[3] == "3");

    rig.platform.link().clear();
    fly(rig, t, 12);
    REQUIRE(flight::state_from(rig.state().own.flight_state) == flight::FlightState::Airborne);
    CHECK(fields(last_of(rig, "$PFLAU"))[3] == "2");
}

// The TX field is what an app shows as transmitting, and a device can hear all and say nothing.
TEST_CASE("nmea: PFLAU's TX field is the transmitter's own gate, so a silent device admits it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    // Nothing goes on air before the first fix has settled, so neither does a TX of 1.
    fly(rig, t, gnss::kFirstFixSettleMs / 1000);
    REQUIRE(rig.state().own.tx_settled);
    REQUIRE(fields(last_of(rig, "$PFLAU"))[2] == "1");

    // No PPS lock is no burst (core/timing/transmit.h), while the fix and the pass stay.
    rig.platform.pps().set_locked(false);
    rig.platform.link().clear();
    fly(rig, t, 2);
    const std::vector<std::string> f = fields(last_of(rig, "$PFLAU"));
    CHECK(f[2] == "0");
    CHECK(f[3] == "2");
    CHECK(rig.state().own.fix_valid);
}

// FLARM's scale is time to impact, and 1 is its lowest real alarm: a 3 km ring
// is the only band that number does not overstate.
TEST_CASE("nmea: an advisory goes out as FLARM alarm level 1") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);

    // Co-altitude off the left wing and well inside the advisory window: the
    // level core/traffic assessed, not one written into the table by hand.
    for (int pass = 0; pass < 4; pass++) {
        hear(rig, 0x112233, 400, 150, 31, /*track_c9=*/223);
        fly(rig, t, 1);
    }
    REQUIRE(rig.state().alarm_level == traffic::Level::Advisory);

    std::string status;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PFLAU,", 0) == 0) status = s;
    REQUIRE_FALSE(status.empty());
    const std::vector<std::string> f = fields(status);
    REQUIRE(f.size() >= 10);
    CHECK(std::stoi(f[1]) >= 1);  // targets heard
    CHECK(std::stoi(f[3]) == 2);  // 3D fix
    CHECK(std::stoi(f[5]) == 1);
    CHECK(std::stoi(f[5]) == static_cast<int>(traffic::to_number(rig.state().alarm_level)));
    // Own-ship is tracking east and the threat is due north of it, so it is off
    // the left wing: the relative bearing is signed, half a turn either way, and
    // an app that drew 270 here would put the arrow on the wrong side.
    CHECK(std::stoi(f[6]) < -60);
    CHECK(std::stoi(f[6]) > -120);
    CHECK(std::stoi(f[7]) == 2);   // alarm type: aircraft
    CHECK(std::stoi(f[9]) < 600);  // relative distance, metres
    CHECK(f[10] == "112233");      // the threat's id

    std::string target;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PFLAA,", 0) == 0) target = s;
    REQUIRE_FALSE(target.empty());
    CHECK(target.rfind("$PFLAA,1,", 0) == 0);
}

TEST_CASE(
    "nmea: every frame fits the payload the central negotiated, down to what BLE guarantees") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // The phone that never exchanges an MTU: 23 bytes of ATT, 20 of payload.
    // A $PFLAA does not fit in one of those, so a sender that framed one
    // sentence per notification would send this pilot nothing at all.
    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayload);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);
    hear(rig, 0xABCDEF, 900, -400, -60);
    fly(rig, t, 2);

    int frames = 0;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != events::Endpoint::Nmea) continue;
        frames++;
        CHECK(frame.bytes.size() <= ports::kMinimumLinkPayload);
    }
    CHECK(frames > 0);
    // The controller's refusal never happened: nothing oversized was offered.
    CHECK(rig.platform.link().refused_oversize == 0);
    // And the stream still reassembles into whole, checksummed sentences.
    const std::vector<std::string> heard = sentences(rig);
    REQUIRE(heard.size() >= 2);
    for (const std::string& s : heard) CHECK(checksum_ok(s));
    bool saw_traffic = false;
    for (const std::string& s : heard) saw_traffic = saw_traffic || s.rfind("$PFLAA,", 0) == 0;
    CHECK(saw_traffic);
}

TEST_CASE("nmea: more targets than one pass carries are all refreshed inside the bound") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 1);

    // A full table: five times what one pass may carry, so the bound below is
    // exercised at the worst case the device can be in rather than near it.
    const int count = traffic::TrafficTable::kCapacity;
    for (int i = 0; i < count; i++) {
        hear(rig, 0x200000u + static_cast<uint32_t>(i), 500 + 100 * i, 200 - 30 * i, 40 + 5 * i);
        // The radio queue holds eight events, and the traffic service drains it
        // once a pass: a sky this busy arrives over several passes, as it does
        // on air.
        if ((i + 1) % 4 == 0) {
            rig.run(t, t + 40);
            t += 50;
        }
    }
    fly(rig, t, 1);
    REQUIRE(rig.state().traffic.count() == count);
    rig.platform.link().clear();

    // One bound's worth of passes, and every aircraft in the table has been
    // named at least once inside it.
    fly(rig, t, go::NmeaService::kTargetRefreshBoundMs / 1000);
    const std::vector<std::string> heard = sentences(rig);
    for (int i = 0; i < count; i++) {
        char id[8];
        std::snprintf(id, sizeof(id), "%06X", 0x200000u + static_cast<uint32_t>(i));
        bool named = false;
        for (const std::string& s : heard)
            named = named || (s.rfind("$PFLAA,", 0) == 0 && s.find(id) != std::string::npos);
        CHECK_MESSAGE(named, "target ", id, " was never refreshed inside the bound");
    }
    // No pass spends its whole budget on traffic and drops the alarm sentence.
    CHECK(count_of(rig, "$PFLAU") >=
          static_cast<int>(go::NmeaService::kTargetRefreshBoundMs / 1000));
    // ...and no pass sends more traffic than the cap the bound is derived from.
    CHECK(count_of(rig, "$PFLAA") <=
          go::NmeaService::kTargetsPerPass *
              (static_cast<int>(go::NmeaService::kTargetRefreshBoundMs / 1000) + 1));
}

// The notify share ended each pass at four 20-byte frames: no $PFLAA reached a 20-byte tablet.
TEST_CASE("nmea: a pass longer than the link's share reaches a tablet at the BLE minimum whole") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    rig.platform.link().declare_payload_bytes(ports::kMinimumLinkPayload);
    fly(rig, t, 1);
    for (int i = 0; i < 4; i++)
        hear(rig, 0x300000u + static_cast<uint32_t>(i), 500 + 100 * i, 200, 40 + 5 * i);
    fly(rig, t, 1);
    REQUIRE(rig.state().traffic.count() == 4);

    rig.platform.link().hold_after(4);
    rig.platform.link().clear();
    for (int second = 0; second < 2; second++) {
        rig.push_timed_fix(25000, 900);
        for (uint32_t ms = 0; ms < 1000; ms += 10) {
            rig.platform.link().serve();
            rig.run(t + ms, t + ms, 10);
        }
        t += 1000;
        rig.utc_offset_s++;
    }

    const std::vector<std::string> heard = sentences(rig);
    for (const std::string& s : heard) CHECK(checksum_ok(s));
    CHECK(count_of(rig, "$PFLAU") >= 2);
    CHECK(count_of(rig, "$PFLAA") >= 4);
    CHECK(rig.product.nmea().link_drops() == 0);
}

TEST_CASE("nmea: $PGRMZ carries pressure altitude on the standard datum") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // The air the aircraft is actually flying through, nowhere near standard.
    rig.platform.baro().chip.set_pressure_mpa(90000000);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().baro.active);

    std::string altitude;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$PGRMZ,", 0) == 0) altitude = s;
    REQUIRE_FALSE(altitude.empty());
    CHECK(checksum_ok(altitude));
    const std::vector<std::string> f = fields(altitude);
    REQUIRE(f.size() >= 3);
    CHECK(f[2] == "F");

    const uint32_t pressure_pa = rig.state().baro.pressure_mpa / 1000;
    const int32_t standard_cm = flight::pressure_to_alt_cm(pressure_pa);
    const int32_t sent_cm = static_cast<int32_t>(std::stoi(f[1])) * 3048 / 100;
    // What an EFB does with this figure is apply its own QNH, so the figure has
    // to be the datum-free one: pressure altitude on 1013.25, within a foot.
    CHECK(standard_cm > 90000);
    CHECK(std::abs(sent_cm - standard_cm) < 40);
}

// G. Battery state reaches the panel and stops there. $LK8EX1 is the one
// sentence LK8000, XCSoar and their descendants already parse that carries a
// cell, and it carries the vario picture with it. Field 5 is a voltage below
// 1000 and a percentage plus 1000 at or above it, which is the distinction the
// whole sentence turns on: 55 in that field is fifty-five volts.
TEST_CASE("nmea: the cell and the temperature reach a tablet in $LK8EX1, on the $PGRMZ pass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    // A cell in the flat middle, and air the aircraft is really flying through.
    rig.platform.battery().millivolts = 3800;
    rig.platform.baro().chip.set_pressure_mpa(90000000);
    rig.platform.baro().chip.set_temperature_decicelsius(-72);
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().baro.active);
    REQUIRE(rig.state().power.battery.valid);

    std::string lk8;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$LK8EX1,", 0) == 0) lk8 = s;
    REQUIRE_FALSE(lk8.empty());
    CHECK(checksum_ok(lk8));

    const std::vector<std::string> f = fields(lk8);
    REQUIRE(f.size() == 6);
    // Field 1 is the raw pressure in pascals, which is what a consumer prefers
    // over field 2 because it can apply its own datum to it.
    CHECK(std::stol(f[1]) == static_cast<long>(rig.state().baro.pressure_mpa / 1000));
    // Field 2 is metres on 1013.25, the same datum-free figure $PGRMZ carries.
    CHECK(std::abs(std::stol(f[2]) -
                   flight::pressure_to_alt_cm(rig.state().baro.pressure_mpa / 1000) / 100) <= 1);
    // Field 5 is the gauge's own percentage, offset by 1000. A device that says
    // 55% on its panel and something else on the tablet is a support call.
    CHECK(std::stol(f[5]) == 1000 + rig.state().power.battery.percent);
    CHECK(std::stol(f[5]) >= 1000);
    CHECK(int(rig.state().power.battery.percent) == 55);
    // Field 4 is what the part measured, carried in the whole degrees the sentence is read in.
    REQUIRE(rig.state().baro.temperature_valid);
    CHECK(rig.state().baro.temperature_decicelsius == -72);
    CHECK(f[4] == "-7");

    // Same pass as $PGRMZ, so the same cadence, second for second.
    rig.platform.link().clear();
    fly(rig, t, 3);
    CHECK(count_of(rig, "$LK8EX1") >= 3);
    CHECK(count_of(rig, "$LK8EX1") == count_of(rig, "$PGRMZ"));
    CHECK(count_of(rig, "$LK8EX1") == count_of(rig, "$PFLAU"));
}

// $PGRMZ is gated on a barometer because a GNSS altitude under that sentence
// name would feed a geometric height into an app's altimeter. $LK8EX1 is not,
// because the cell is not a barometric quantity and this is the only sentence we
// speak that says anything about power: silence here is a pilot with no way to
// see a flat unit coming. SoftRF MB sends the same battery-only sentence when no
// baro chip answered (src/protocol/data/NMEA.cpp:1398-1401).
TEST_CASE("nmea: a unit with no barometer still tells the tablet about its cell") {
    Rig rig(kBaroByHand);
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    rig.platform.battery().millivolts = 3600;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE_FALSE(rig.state().baro.active);
    REQUIRE(rig.state().power.battery.valid);

    // No $PGRMZ at all, and an $LK8EX1 every second regardless.
    CHECK(count_of(rig, "$PGRMZ") == 0);
    CHECK(count_of(rig, "$LK8EX1") >= 2);

    std::string lk8;
    for (const std::string& s : sentences(rig))
        if (s.rfind("$LK8EX1,", 0) == 0) lk8 = s;
    REQUIRE_FALSE(lk8.empty());
    CHECK(checksum_ok(lk8));

    const std::vector<std::string> f = fields(lk8);
    REQUIRE(f.size() == 6);
    CHECK(f[1] == "999999");  // no pressure
    CHECK(f[2] == "99999");   // no pressure altitude
    CHECK(f[4] == "99");      // no temperature
    CHECK(std::stol(f[5]) == 1000 + rig.state().power.battery.percent);
    CHECK(int(rig.state().power.battery.percent) == 12);
}

// The cadence arithmetic this relies on: emit_ownship() is two calls inside
// run_pass(), the same pass PFLAU and PGRMZ already share, at the pass's fixed
// 1 Hz. NmeaService::kTargetsPerPass and kPassesPerRefreshBound are derived
// only from kTargetRefreshBoundMs, kMovingTargetRedrawMs and the table's
// capacity - none of which this reads or writes - so two more sentences a pass
// changes what a pass costs in bytes, never how many passes the refresh bound
// allows or how many targets one may carry.
TEST_CASE("nmea: GPRMC/GPGGA give a panel-mounted tablet the position it has no GNSS for") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    fly(rig, t, 3);
    rig.raise_link();
    fly(rig, t, 2);
    REQUIRE(rig.state().own.fix_valid);
    REQUIRE(rig.state().own.utc_valid);

    std::string rmc, gga;
    for (const std::string& s : sentences(rig)) {
        if (s.rfind("$GPRMC,", 0) == 0) rmc = s;
        if (s.rfind("$GPGGA,", 0) == 0) gga = s;
    }
    REQUIRE_FALSE(rmc.empty());
    REQUIRE_FALSE(gga.empty());
    CHECK(checksum_ok(rmc));
    CHECK(checksum_ok(gga));

    const std::vector<std::string> rf = fields(rmc);
    REQUIRE(rf.size() >= 10);
    CHECK(rf[2] == "A");       // status: a valid fix, not void
    CHECK(rf[1].size() == 6);  // hhmmss
    CHECK(rf[9].size() == 6);  // ddmmyy

    const std::vector<std::string> gf = fields(gga);
    REQUIRE(gf.size() >= 9);
    CHECK(gf[6] == "1");          // fix quality: a fix
    CHECK(std::stoi(gf[7]) > 0);  // satellites, as the rig's fix reports them

    // One pass a second, one of each per pass: two more seconds of flight is
    // two more of each, same as the $PFLAU heartbeat they now share a pass with.
    rig.platform.link().clear();
    fly(rig, t, 2);
    CHECK(count_of(rig, "$GPRMC") >= 2);
    CHECK(count_of(rig, "$GPGGA") >= 2);
    CHECK(count_of(rig, "$GPRMC") == count_of(rig, "$PFLAU"));
}

namespace {

// The service alone, with a link that is up and a fix that is valid, so that the
// only thing a case changes is what the product claims to be.
struct FeatureRig {
    platform::host::Clock clock;
    platform::host::Link link;
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          link,     null.display,         null.kv,
        null.log_flash, null.annunciator, null.dfu, null.die_temperature, null.indicator,
        null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::SettingsStore store{settings, roles.device_addr};
    comms::ConfigService config{link, store};
    go::NmeaService nmea;

    FeatureRig(go::Feature declared, ports::Capabilities fitted = ports::Capability::Link)
        : nmea(context, declared, config) {
        roles.capabilities = fitted;
        state.own.fix_valid = true;
        state.own.utc_valid = true;
        state.own.lat_1e7 = 485000000;
        state.own.lon_1e7 = 85000000;
        config.on_link_up(events::LinkUp{1, platform::host::Link::kDefaultPayloadBytes});
        REQUIRE(nmea.setup() == Status::Ok);
    }

    int frames() { return link.count_on(events::Endpoint::Nmea); }
};

}  // namespace

TEST_CASE("nmea: a product that does not declare the companion link says nothing on it") {
    FeatureRig silent(go::Feature::None);
    for (uint32_t t = 0; t <= 5000; t += 100) silent.nmea.tick(t);
    CHECK_FALSE(silent.nmea.enabled());
    CHECK(silent.frames() == 0);

    // The same rig, the same link, the same fix: the feature is the difference,
    // which is what makes it a gate and not decoration.
    FeatureRig speaking(go::Feature::CompanionLink);
    for (uint32_t t = 0; t <= 5000; t += 100) speaking.nmea.tick(t);
    CHECK(speaking.nmea.enabled());
    CHECK(speaking.frames() > 0);
}

TEST_CASE("nmea: the companion link is not claimed on a board with no link fitted") {
    FeatureRig unfitted(go::Feature::CompanionLink, ports::Capability::None);
    for (uint32_t t = 0; t <= 5000; t += 100) unfitted.nmea.tick(t);
    CHECK_FALSE(unfitted.nmea.enabled());
    CHECK(unfitted.frames() == 0);
}

// M. The pass cadence across the 49.7-day wrap of ports::Clock::millis(). The
// service defers a pass while a burst is armed and otherwise redraws every second,
// both of them measured as differences from the last pass, and the flag beside the
// stamp is what keeps a zero from meaning "never passed". If it were an instant
// comparison, a paired tablet would go quiet for seven weeks with a device that is
// tracking perfectly well behind it - and $PFLAU going quiet is what XCSoar reads
// as the device having failed.
TEST_CASE("nmea: a paired tablet keeps hearing the device across the 49.7-day wrap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0xFFFFFF00u - 2000u;  // a couple of seconds short of the wrap
    rig.raise_link();
    for (int i = 0; i < 2; i++) rig.second_across(t, 25000, 900);
    REQUIRE(rig.link_up());
    REQUIRE(count_of(rig, "$PFLAU") >= 2);

    // Four more seconds, stepped straight through zero.
    rig.platform.link().clear();
    for (int i = 0; i < 4; i++) rig.second_across(t, 25000, 900);
    CHECK(count_of(rig, "$PFLAU") >= 4);
    for (const std::string& s : sentences(rig)) CHECK(checksum_ok(s));
}
