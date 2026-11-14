// DF17 from the LR2021's chips to typed messages, pinned to the decodes protocols/adsb/README
// names.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsb/message.h"
#include "protocols/adsb/test/receiver.h"

using namespace skyblip;
using namespace skyblip::adsb;
using skyblip::wire::Status;

namespace {

constexpr adsb::Reception kReception{123456789, -71};

struct Heard {
    Status status;
    adsb::Message message;
};

using Frame = AdsbFrame;

Frame extended_squitter(uint8_t type_code) {
    Frame frame = frame_of(
        "8d4840d6"
        "00000000000000000000");
    adsb_set_message_bits(frame, 1, 5, type_code);
    return frame;
}

Heard hear_chips(const uint8_t* chips) {
    Heard heard{Status::Ok, {}};
    heard.status = adsb::receive(chips, adsb::kChipBytes, kReception, heard.message);
    return heard;
}

Heard hear_sealed(Frame frame) {
    adsb_seal(frame.bytes);
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    return hear_chips(chips);
}

Heard hear(const char* text) {
    const Frame frame = frame_of(text);
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    return hear_chips(chips);
}

// INFO: fc 10nov26 either chip flipped makes a doubtful pair, and the decoder reads the second one
void blur(uint8_t* chips, uint8_t frame_bit, bool decodes_wrong) {
    const uint16_t chip = static_cast<uint16_t>(2 * (frame_bit - 4) + (decodes_wrong ? 1 : 0));
    chips[chip >> 3] = static_cast<uint8_t>(chips[chip >> 3] ^ (0x80u >> (chip & 7)));
}

int32_t pressure_altitude_of(uint32_t field) {
    Frame frame = extended_squitter(11);
    adsb_set_message_bits(frame, 9, 20, field);
    const Heard heard = hear_sealed(frame);
    REQUIRE(heard.status == Status::Ok);
    REQUIRE(heard.message.airborne_position.pressure_altitude_valid);
    return heard.message.airborne_position.pressure_altitude_ft;
}

// INFO: fc 10nov26 Wikipedia prints Gillham as D1 D2 D4 A1 A2 A4 B1 B2 B4 C1 C2 C4
uint32_t gillham_field(const char* printed) {
    enum Printed { D1, D2, D4, A1, A2, A4, B1, B2, B4, C1, C2, C4 };
    const int field_order[] = {C1, A1, C2, A2, C4, A4, B1, D1, B2, D2, B4, D4};
    uint32_t field = 0;
    for (int position : field_order)
        field = (field << 1) | static_cast<uint32_t>(printed[position] - '0');
    return field;
}

}

TEST_CASE("adsb: the Riddle's identification frame names KLM1023 with no category") {
    const Heard heard = hear("8d4840d6202cc371c32ce0576098");
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.kind == adsb::Kind::Identification);
    CHECK(heard.message.address == 0x4840D6);
    CHECK(heard.message.identification.category_set == 'A');
    CHECK(heard.message.identification.category == 0);
    CHECK(std::strcmp(heard.message.identification.callsign, "KLM1023") == 0);
}

TEST_CASE("adsb: dump1090's identifications name their callsign and category") {
    const Heard medium = hear("8dc063662240f172d71ce01973ee");
    REQUIRE(medium.status == Status::Ok);
    CHECK(std::strcmp(medium.message.identification.callsign, "POE2513") == 0);
    CHECK(medium.message.identification.category == 2);

    const Heard light = hear("8fc01235215073f9e36820ace10d");
    REQUIRE(light.status == Status::Ok);
    CHECK(light.message.capability == 7);
    CHECK(std::strcmp(light.message.identification.callsign, "TGO986") == 0);
    CHECK(light.message.identification.category == 1);
}

TEST_CASE("adsb: the type code names the category set, D at 1 up to A at 4") {
    Frame frame = extended_squitter(3);
    adsb_set_message_bits(frame, 6, 8, 1);
    adsb_set_message_bits(frame, 9, 14, 7);
    adsb_set_message_bits(frame, 15, 56, 0);
    for (uint8_t i = 1; i < adsb::kCallsignCharacters; i++)
        adsb_set_message_bits(frame, static_cast<uint8_t>(9 + 6 * i),
                              static_cast<uint8_t>(14 + 6 * i), 32);
    const Heard glider = hear_sealed(frame);
    REQUIRE(glider.status == Status::Ok);
    CHECK(glider.message.identification.category_set == 'B');
    CHECK(glider.message.identification.category == 1);
    CHECK(std::strcmp(glider.message.identification.callsign, "G") == 0);
}

TEST_CASE("adsb: a callsign with a code outside the character set is refused whole") {
    Frame frame = extended_squitter(4);
    adsb_set_message_bits(frame, 9, 14, 11);
    adsb_set_message_bits(frame, 15, 20, 0);
    const Heard heard = hear_sealed(frame);
    REQUIRE(heard.status == Status::Ok);
    CHECK_FALSE(heard.message.identification.callsign_valid);
    CHECK(heard.message.identification.callsign[0] == 0);
}

TEST_CASE("adsb: the Riddle's airborne pair reads 38000 ft and both CPR halves") {
    const Heard even = hear("8d40621d58c382d690c8ac2863a7");
    REQUIRE(even.status == Status::Ok);
    CHECK(even.message.kind == adsb::Kind::AirbornePosition);
    CHECK(even.message.airborne_position.pressure_altitude_ft == 38000);
    CHECK_FALSE(even.message.airborne_position.cpr.odd);
    CHECK(even.message.airborne_position.cpr.latitude_cpr == 93000);
    CHECK(even.message.airborne_position.cpr.longitude_cpr == 51372);

    const Heard odd = hear("8d40621d58c386435cc412692ad6");
    REQUIRE(odd.status == Status::Ok);
    CHECK(odd.message.airborne_position.cpr.odd);
    CHECK(odd.message.airborne_position.cpr.latitude_cpr == 74158);
    CHECK(odd.message.airborne_position.cpr.longitude_cpr == 50194);
}

TEST_CASE("adsb: dump1090's Q-bit and Gillham frames read 11350 ft and 7800 ft") {
    const Heard q_bit = hear("8dc080d9583de5a6ecabbfe4e2d1");
    REQUIRE(q_bit.status == Status::Ok);
    CHECK(q_bit.message.airborne_position.pressure_altitude_ft == 11350);

    const Heard gillham = hear("8dc027dc591ca5c6a2a9cdff8b81");
    REQUIRE(gillham.status == Status::Ok);
    CHECK(gillham.message.airborne_position.pressure_altitude_ft == 7800);
    CHECK(gillham.message.airborne_position.cpr.latitude_cpr == 58193);
    CHECK(gillham.message.airborne_position.cpr.longitude_cpr == 43469);
}

TEST_CASE("adsb: Gillham codes decode in 100 ft steps, reflected when the 500 ft count is odd") {
    CHECK(pressure_altitude_of(gillham_field("000000000001")) == -1200);
    CHECK(pressure_altitude_of(gillham_field("000000011010")) == 0);
    CHECK(pressure_altitude_of(gillham_field("000000010100")) == 300);
    CHECK(pressure_altitude_of(gillham_field("000000100001")) == 2700);
    CHECK(pressure_altitude_of(gillham_field("010000000001")) == 126700);
}

TEST_CASE("adsb: an altitude of all zeros, or a Gillham code no encoder sends, is no altitude") {
    Frame frame = extended_squitter(11);
    adsb_set_message_bits(frame, 23, 39, 0x12345);
    const Heard zero = hear_sealed(frame);
    REQUIRE(zero.status == Status::Ok);
    CHECK_FALSE(zero.message.airborne_position.pressure_altitude_valid);

    adsb_set_message_bits(frame, 9, 20, gillham_field("000000011000"));
    const Heard unused = hear_sealed(frame);
    REQUIRE(unused.status == Status::Ok);
    CHECK_FALSE(unused.message.airborne_position.pressure_altitude_valid);
}

// DO-260B draft 4.2 2.2.3.2.3.4.2 codes GNSS height by 2.2.3.2.3.4.3, as pressure altitude is.
TEST_CASE("adsb: a GNSS height uses the pressure altitude's coding and is no pressure altitude") {
    Frame frame = extended_squitter(20);
    adsb_set_message_bits(frame, 9, 20, 0xB50);
    const Heard height = hear_sealed(frame);
    REQUIRE(height.status == Status::Ok);
    CHECK(height.message.type_code == 20);
    CHECK(height.message.airborne_position.gnss_height_valid);
    CHECK(height.message.airborne_position.gnss_height_ft == 35000);
    CHECK_FALSE(height.message.airborne_position.pressure_altitude_valid);

    adsb_set_message_bits(frame, 9, 20, 0);
    const Heard none = hear_sealed(frame);
    REQUIRE(none.status == Status::Ok);
    CHECK_FALSE(none.message.airborne_position.gnss_height_valid);
}

TEST_CASE("adsb: the Riddle's surface frame moves at 17 kt on a track of 33 steps") {
    const Heard heard = hear("8c4841753a9a153237aef0f275be");
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.kind == adsb::Kind::SurfacePosition);
    CHECK(heard.message.capability == 4);
    CHECK(heard.message.surface_position.speed_mkt.value == 17000);
    CHECK(heard.message.surface_position.track_valid);
    CHECK(heard.message.surface_position.track_c7 == 33);
    CHECK(heard.message.surface_position.cpr.odd);
    CHECK(heard.message.surface_position.cpr.latitude_cpr == 39195);
    CHECK(heard.message.surface_position.cpr.longitude_cpr == 110320);
}

TEST_CASE("adsb: movement codes read the floor of their band, none, stopped and 175 kt or more") {
    auto speed_of = [](uint32_t code) {
        Frame frame = extended_squitter(7);
        adsb_set_message_bits(frame, 6, 12, code);
        const Heard heard = hear_sealed(frame);
        REQUIRE(heard.status == Status::Ok);
        return heard.message.surface_position.speed_mkt;
    };
    CHECK_FALSE(speed_of(0).valid);
    CHECK(speed_of(1).value == 0);
    CHECK(speed_of(2).value == 125);
    CHECK(speed_of(12).value == 1750);
    CHECK(speed_of(38).value == 14500);
    CHECK(speed_of(108).value == 98000);
    CHECK(speed_of(123).value == 170000);
    CHECK(speed_of(124).saturated);
    CHECK(speed_of(124).value == 175000);
    CHECK_FALSE(speed_of(125).valid);
}

TEST_CASE("adsb: the Riddle's ground speed frame reads its components, a GNSS descent and 550 ft") {
    const Heard heard = hear("8d485020994409940838175b284f");
    REQUIRE(heard.status == Status::Ok);
    const adsb::Velocity& velocity = heard.message.velocity;
    CHECK(velocity.subtype == 1);
    CHECK(velocity.east_kt.value == -8);
    CHECK(velocity.north_kt.value == -159);
    CHECK(velocity.climb_fpm.value == -832);
    CHECK(velocity.climb_source == adsb::HeightSource::Gnss);
    CHECK(velocity.gnss_minus_pressure_ft.value == 550);
}

TEST_CASE("adsb: the Riddle's airspeed frame reads 375 kt true on heading 694 steps") {
    const Heard heard = hear("8da05f219b06b6af189400cbc33f");
    REQUIRE(heard.status == Status::Ok);
    const adsb::Velocity& velocity = heard.message.velocity;
    CHECK(velocity.subtype == 3);
    CHECK(velocity.heading_valid);
    CHECK(velocity.heading_c10 == 694);
    CHECK(velocity.airspeed_type == adsb::Airspeed::True);
    CHECK(velocity.airspeed_kt.value == 375);
    CHECK(velocity.climb_fpm.value == -2304);
    CHECK(velocity.climb_source == adsb::HeightSource::Pressure);
    CHECK_FALSE(velocity.gnss_minus_pressure_ft.valid);
}

TEST_CASE("adsb: dump1090's velocities read the speed, rate and GNSS difference it prints") {
    const Heard climbing = hear("8dc080d9990d2c06c0548e2f83fe");
    REQUIRE(climbing.status == Status::Ok);
    const adsb::Velocity& first = climbing.message.velocity;
    CHECK(static_cast<int>(std::hypot(first.east_kt.value, first.north_kt.value)) == 303);
    CHECK(first.climb_fpm.value == 1280);
    CHECK(first.gnss_minus_pressure_ft.value == -325);

    const Heard level = hear("8dc0636699095b0eb004834b3e47");
    REQUIRE(level.status == Status::Ok);
    const adsb::Velocity& second = level.message.velocity;
    CHECK(static_cast<int>(std::hypot(second.east_kt.value, second.north_kt.value)) == 364);
    CHECK(second.climb_fpm.valid);
    CHECK(second.climb_fpm.value == 0);
    CHECK(second.gnss_minus_pressure_ft.value == -50);
}

TEST_CASE("adsb: velocity zeros mean nothing sent and top codes saturate") {
    Frame frame = extended_squitter(19);
    adsb_set_message_bits(frame, 6, 8, 1);
    const Heard zeros = hear_sealed(frame);
    REQUIRE(zeros.status == Status::Ok);
    CHECK_FALSE(zeros.message.velocity.east_kt.valid);
    CHECK_FALSE(zeros.message.velocity.climb_fpm.valid);
    CHECK_FALSE(zeros.message.velocity.gnss_minus_pressure_ft.valid);

    adsb_set_message_bits(frame, 15, 24, 1023);
    adsb_set_message_bits(frame, 37, 46, 0x200 | 511);
    adsb_set_message_bits(frame, 50, 56, 127);
    const adsb::Velocity top = hear_sealed(frame).message.velocity;
    CHECK(top.east_kt.saturated);
    CHECK(top.east_kt.value == 1022);
    CHECK(top.climb_fpm.saturated);
    CHECK(top.climb_fpm.value == -32640);
    CHECK(top.gnss_minus_pressure_ft.saturated);
    CHECK(top.gnss_minus_pressure_ft.value == 3150);
}

TEST_CASE("adsb: supersonic subtypes count speed in 4 kt steps") {
    Frame frame = extended_squitter(19);
    adsb_set_message_bits(frame, 6, 8, 2);
    adsb_set_message_bits(frame, 26, 35, 101);
    CHECK(hear_sealed(frame).message.velocity.north_kt.value == 400);

    adsb_set_message_bits(frame, 6, 8, 4);
    CHECK(hear_sealed(frame).message.velocity.airspeed_kt.value == 400);
}

TEST_CASE("adsb: dump1090's operational status frames read version 2 and their GVA") {
    const Heard two = hear("8dc080d9f8210002004ab8b5c48a");
    REQUIRE(two.status == Status::Ok);
    CHECK(two.message.kind == adsb::Kind::OperationalStatus);
    CHECK(two.message.operational_status.version == 2);
    CHECK(two.message.operational_status.gva_valid);
    CHECK(two.message.operational_status.gva == 2);

    const Heard one = hear("8daa7988f8230006004878278fc6");
    REQUIRE(one.status == Status::Ok);
    CHECK(one.message.operational_status.gva == 1);
}

TEST_CASE("adsb: GVA is read only from an airborne version 2 status") {
    Frame frame = extended_squitter(31);
    adsb_set_message_bits(frame, 49, 50, 3);
    const Heard zero = hear_sealed(frame);
    REQUIRE(zero.status == Status::Ok);
    CHECK(zero.message.operational_status.version == 0);
    CHECK_FALSE(zero.message.operational_status.gva_valid);

    adsb_set_message_bits(frame, 41, 43, 1);
    CHECK_FALSE(hear_sealed(frame).message.operational_status.gva_valid);

    adsb_set_message_bits(frame, 41, 43, 2);
    adsb_set_message_bits(frame, 6, 8, 1);
    const Heard surface = hear_sealed(frame);
    CHECK(surface.message.operational_status.subtype == 1);
    CHECK_FALSE(surface.message.operational_status.gva_valid);
}

TEST_CASE("adsb: a message carries the reception it was heard with") {
    const Heard heard = hear("8d4840d6202cc371c32ce0576098");
    CHECK(heard.message.reception.at_us == kReception.at_us);
    CHECK(heard.message.reception.rssi_dbm == kReception.rssi_dbm);
}

TEST_CASE("adsb: one doubtful chip pair that broke the parity is repaired") {
    const Frame frame = frame_of("8d40621d58c382d690c8ac2863a7");
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    blur(chips, 60, true);
    const Heard heard = hear_chips(chips);
    REQUIRE(heard.status == Status::Ok);
    CHECK(heard.message.airborne_position.pressure_altitude_ft == 38000);
    CHECK(heard.message.airborne_position.cpr.latitude_cpr == 93000);
}

TEST_CASE("adsb: doubtful chip pairs that still decode right pass the parity untouched") {
    const Frame frame = frame_of("8d40621d58c382d690c8ac2863a7");
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    blur(chips, 40, false);
    blur(chips, 90, false);
    blur(chips, 100, false);
    CHECK(hear_chips(chips).status == Status::Ok);
}

TEST_CASE("adsb: a wrong bit beside a second doubtful pair, or one no chip marked, is refused") {
    const Frame frame = frame_of("8d40621d58c382d690c8ac2863a7");
    uint8_t chips[adsb::kChipBytes];
    adsb_chips(frame.bytes, chips);
    blur(chips, 60, true);
    blur(chips, 70, false);
    CHECK(hear_chips(chips).status == Status::Crc);

    adsb_chips(frame.bytes, chips);
    blur(chips, 60, false);
    blur(chips, 60, true);
    CHECK(hear_chips(chips).status == Status::Crc);
}

TEST_CASE("adsb: DF16, an unparsed type code and a short report are refused") {
    Frame acas = frame_of("8d4840d6202cc371c32ce0576098");
    acas.bytes[0] = 0x85;
    CHECK(hear_sealed(acas).status == Status::Unsupported);

    const Heard no_position = hear_sealed(extended_squitter(0));
    CHECK(no_position.status == Status::Unsupported);
    CHECK(no_position.message.address == 0x4840D6);

    uint8_t chips[adsb::kChipBytes]{};
    adsb::Message message;
    CHECK(adsb::receive(chips, adsb::kChipBytes - 1, kReception, message) == Status::Invalid);
}

TEST_CASE("adsb: Bravo's README example fails its parity and reads KLM1023 once mended") {
    uint8_t chips[adsb::kChipBytes];
    const char* reported = "a665956555a669595559a5a55a6a16a55a59a5a955666a69579695";
    for (uint8_t i = 0; i < adsb::kChipBytes; i++)
        chips[i] =
            static_cast<uint8_t>(hex_value(reported[2 * i]) << 4 | hex_value(reported[2 * i + 1]));
    CHECK(hear_chips(chips).status == Status::Crc);

    // Bit 61 is the FIFO chips 112 and 113, the high two bits of chip byte 14.
    uint8_t sound[adsb::kChipBytes];
    adsb_chips(frame_of("8d4840d6202cc371c32ce0576098").bytes, sound);
    chips[14] = static_cast<uint8_t>((chips[14] & 0x3F) | (sound[14] & 0xC0));
    const Heard heard = hear_chips(chips);
    REQUIRE(heard.status == Status::Ok);
    CHECK(std::strcmp(heard.message.identification.callsign, "KLM1023") == 0);
}
