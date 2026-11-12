// Bravo alone: what 1090 MHz becomes on a phone's map, with and without its own fix.
#include <algorithm>
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraft = 0x3C6586;

std::string field(const std::string& sentence, int index) {
    size_t at = 0;
    for (int i = 0; i < index; i++) at = sentence.find(',', at) + 1;
    return sentence.substr(at, sentence.find_first_of(",*", at) - at);
}

bool pflaa_names(BravoRig& rig, uint32_t address) {
    char hex[7];
    for (int i = 0; i < 6; i++) hex[i] = "0123456789ABCDEF"[(address >> (20 - 4 * i)) & 0xF];
    hex[6] = 0;
    for (const std::string& sentence : rig.sentences())
        if (sentence.rfind("$PFLAA", 0) == 0 && sentence.find(hex) != std::string::npos)
            return true;
    return false;
}

}  // namespace

TEST_CASE("bravo: with a fix an aircraft heard on 1090 MHz reaches the phone as $PFLAA") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.seconds_with_fix(3);
    rig.hear_aircraft(kAircraft, 2000);
    rig.seconds_with_fix(2);

    CHECK(rig.state().traffic.count() == 1);
    CHECK(pflaa_names(rig, kAircraft));
    CHECK(field(rig.last_of("$PFLAU"), 3) != "0");
    CHECK(rig.count_of("$GPRMC") >= 2);
    CHECK(rig.count_of("$GPGGA") >= 2);
}

TEST_CASE("bravo: without a fix it says GPS 0, sends no $PFLAA and keeps decoding") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.seconds_blind(3);
    rig.hear_aircraft(kAircraft, 2000);
    rig.seconds_blind(2);

    CHECK(rig.product.adsb().folded() == 2);
    CHECK(rig.product.adsb().assembly().count() == 1);
    CHECK_FALSE(pflaa_names(rig, kAircraft));
    CHECK(field(rig.last_of("$PFLAU"), 3) == "0");
    CHECK(rig.lamp() == indication::Condition::NoFixLinked);
}

TEST_CASE("bravo: a fix that comes back does not make an aircraft heard before it fresh") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.raise_link();
    rig.seconds_blind(2);
    rig.hear_aircraft(kAircraft, 2000);
    rig.seconds_blind(3);
    rig.platform.link().clear();
    rig.seconds_with_fix(3);
    CHECK_FALSE(pflaa_names(rig, kAircraft));

    rig.hear_aircraft(kAircraft, 2000);
    rig.seconds_with_fix(2);
    CHECK(pflaa_names(rig, kAircraft));
}

TEST_CASE("bravo: the aircraft's own transponder, named by own ICAO, is never traffic") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.product.settings().own_icao = kAircraft;
    rig.seconds_with_fix(2);
    rig.hear_aircraft(kAircraft, 500);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(2);

    CHECK(rig.product.adsb().assembly().count() == 1);
    CHECK(rig.state().traffic.find(protocol::kIcaoAddressMappingTable, kAircraft) < 0);
    CHECK(rig.state().traffic.count() == 1);
}

TEST_CASE("bravo: a frame that fails its parity is counted as crc and kept for the console") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    AdsbFrame frame = adsb_airborne_position(kAircraft, 0, 48.51, 8.5, false, 1800);
    adsb_seal(frame.bytes);
    frame.bytes[5] ^= 0x10;
    const AdsbAir air = [&] {
        AdsbAir out;
        out.chips[0] = kAdsbPreambleAir[0];
        out.chips[1] = kAdsbPreambleAir[1];
        out.chips[2] = protocol::kAdsbSyncChips;
        adsb_chips(frame.bytes, out.chips + 3);
        return out;
    }();
    rig.platform.radio().receive_air(air.chips, sizeof(air.chips), -87);
    rig.seconds_with_fix(1);

    CHECK(rig.product.adsb().crc() == 1);
    CHECK(rig.product.adsb().unsupported() == 0);
    CHECK(rig.product.adsb().folded() == 0);
    bravo::AdsbReject reject{};
    REQUIRE(rig.product.adsb().take_reject(reject));
    CHECK(reject.status == Status::Crc);
    CHECK(reject.rssi_valid);
    CHECK(reject.rssi_dbm == -87);
    CHECK(std::equal(reject.chips.begin(), reject.chips.end(), air.chips + 3));
    CHECK_FALSE(rig.product.adsb().take_reject(reject));
}

TEST_CASE("bravo: a sound DF17 of a type the decoder skips is counted unsupported, not crc") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    rig.hear(adsb_frame(kAircraft, 0, 28));
    rig.seconds_with_fix(1);

    CHECK(rig.product.adsb().unsupported() == 1);
    CHECK(rig.product.adsb().crc() == 0);
    bravo::AdsbReject reject{};
    REQUIRE(rig.product.adsb().take_reject(reject));
    CHECK(reject.status == Status::Unsupported);
}

// The console drains the rejects each pass, so a full ring is a console that stopped reading.
TEST_CASE(
    "bravo: the rejects waiting for the console are the first ones, and every one is counted") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    const int heard = static_cast<int>(bravo::AdsbService::kRejectsKept) + 8;
    for (int i = 0; i < heard; i++) {
        events::AdsbFrame noise{};
        noise.at_us = rig.platform.clock().microseconds();
        noise.rssi_dbm = static_cast<int8_t>(-120 + i);
        noise.rssi_valid = true;
        noise.chips.fill(0x55);
        rig.product.adsb_frames().push(noise);
        rig.step();
    }

    CHECK(rig.product.adsb().crc() == static_cast<uint32_t>(heard));
    bravo::AdsbReject reject{};
    int kept = 0;
    while (rig.product.adsb().take_reject(reject)) {
        CHECK(reject.rssi_dbm == -120 + kept);
        kept++;
    }
    CHECK(kept == static_cast<int>(bravo::AdsbService::kRejectsKept));
}

// Opus's review: a frame the LR2021 gave no level reached the observation as 0 dBm.
TEST_CASE(
    "bravo: an aircraft heard with no level carries none, and one heard with a level keeps it") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(2);
    for (bool odd : {false, true}) {
        AdsbFrame frame = adsb_airborne_position(kAircraft, 0, 48.51, 8.5, odd, 1800);
        adsb_seal(frame.bytes);
        events::AdsbFrame heard{};
        heard.at_us = rig.platform.clock().microseconds();
        heard.rssi_valid = false;
        adsb_chips(frame.bytes, heard.chips.data());
        rig.product.adsb_frames().push(heard);
        rig.step();
    }
    rig.seconds_with_fix(1);
    const int slot = rig.state().traffic.find(protocol::kIcaoAddressMappingTable, kAircraft);
    REQUIRE(slot >= 0);
    CHECK_FALSE(rig.state().traffic.at(slot)->observation.rssi_valid);

    rig.hear_aircraft(0x4CA2B1, 2000);
    rig.seconds_with_fix(1);
    const int other = rig.state().traffic.find(protocol::kIcaoAddressMappingTable, 0x4CA2B1);
    REQUIRE(other >= 0);
    CHECK(rig.state().traffic.at(other)->observation.rssi_valid);
    CHECK(rig.state().traffic.at(other)->observation.rssi_dbm == -70);
}
