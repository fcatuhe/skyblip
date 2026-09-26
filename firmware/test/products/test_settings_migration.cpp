// A stored blob is a data format, so every version a firmware ever wrote comes back as itself,
// less only the fields that left, and routed by its version byte.
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/fec/crc.h"
#include "core/power/battery.h"
#include "core/settings/address.h"
#include "core/util/json_min.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;
using namespace skyblip::settings;

// B4. A stored blob is a data format: deleting a field is expand, migrate,
// contract, and the migration is the part a device in the field depends on.
TEST_CASE("settings: a blob written by version-1 firmware comes back as itself") {
    // The version-1 payload, byte for byte as that firmware memcpy'd its struct:
    // region, rotation and power_save sat between aircraft_type and callsign.
    struct V1 {
        uint8_t version{1};
        uint32_t device_addr{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        uint8_t region{0};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t rotation{0};
        uint8_t page_mask{0x0F};
        bool power_save{false};
        char callsign[10]{0};
    };

    V1 old{};
    old.device_addr = 0x5B7E57;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.region = 2;         // a field this firmware no longer has
    old.rotation = 3;       // and another
    old.power_save = true;  // and the third
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Nautical;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 1;
    std::memcpy(blob + 1, &old, sizeof(V1));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V1));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V1) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V1) + 4, out) == Status::Ok);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.units == Units::Nautical);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    CHECK(int(out.version) == int(Settings::kCurrentVersion));

    // And what it writes back is the current layout, not the one it read.
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    Settings again;
    CHECK(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(std::string(again.callsign) == "D-KXYZ");
}

// H, the migration. The battery trim went in beside the address rather than at
// the end, which costs no padding, so version 3 is the same LENGTH as version 2
// and a different layout. A length check cannot tell them apart and the version
// byte must: read the old bytes as the new struct and a page_mask of 15 becomes
// a callsign, an aircraft type becomes a trim of several hundred millivolts.
TEST_CASE("settings: a blob written by version-2 firmware comes back as itself, untrimmed") {
    // The version-2 payload, byte for byte as that firmware memcpy'd its struct.
    struct V2 {
        uint8_t version{1};
        uint32_t device_addr{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t page_mask{0x0F};
        char callsign[10]{0};
    };

    V2 old{};
    old.device_addr = 0x5B7E57;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Nautical;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 2;
    std::memcpy(blob + 1, &old, sizeof(V2));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V2));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V2) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V2) + 4, out) == Status::Ok);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.units == Units::Nautical);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    // A unit that stored its settings before the trim existed was never
    // calibrated, so it comes back reading exactly as it did yesterday.
    CHECK(int(out.battery_offset_mv) == 0);

    // And what it writes back is the current layout, which the current firmware
    // reads and the old blob's version byte no longer claims.
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    CHECK(int(kBlobVersion) == 9);
}

// Before version 9 the only way to hold a trim was for somebody to measure one.
TEST_CASE("settings: a trim stored by version-8 firmware is a hand-set trim") {
    struct V8 {
        uint8_t version{1};
        int16_t battery_offset_mv{0};
        int16_t freq_trim_e1_ppm{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        Units units{Units::Metric};
        char callsign[10]{0};
    };

    V8 old{};
    old.battery_offset_mv = -120;
    old.freq_trim_e1_ppm = -37;
    old.aircraft_type = 9;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 8;
    std::memcpy(blob + 1, &old, sizeof(V8));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V8));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V8) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V8) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(out.battery_offset_manual);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
    CHECK(std::string(out.callsign) == "D-KXYZ");

    // A unit that stored no trim never had one measured, so the charger may set it.
    V8 untrimmed{};
    uint8_t plain[128] = {0};
    plain[0] = 8;
    std::memcpy(plain + 1, &untrimmed, sizeof(V8));
    const uint32_t plain_crc = fec::crc32(plain, 1 + sizeof(V8));
    for (int i = 0; i < 4; i++)
        plain[1 + sizeof(V8) + i] = static_cast<uint8_t>(plain_crc >> (8 * i));

    Settings fresh;
    REQUIRE(from_blob(plain, 1 + sizeof(V8) + 4, fresh) == Status::Ok);
    CHECK_FALSE(fresh.battery_offset_manual);
}

// M, the migration: the address and its table are the device's now, so they leave the blob.
TEST_CASE("settings: a blob written by version-7 firmware comes back without its identity") {
    struct V7 {
        uint8_t version{1};
        uint32_t device_addr{0};
        int16_t battery_offset_mv{0};
        int16_t freq_trim_e1_ppm{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        Units units{Units::Metric};
        char callsign[10]{0};
    };

    V7 old{};
    old.device_addr = 0xDD1234;
    old.battery_offset_mv = -120;
    old.freq_trim_e1_ppm = -37;
    old.addr_table = 5;
    old.aircraft_type = 9;
    old.alarm_volume = 5;
    old.units = Units::Nautical;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 7;
    std::memcpy(blob + 1, &old, sizeof(V7));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V7));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V7) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V7) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
    CHECK(int(out.aircraft_type) == 9);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(std::string(out.callsign) == "D-KXYZ");

    char buf[256];
    const int n = to_json(out, 0x123456, buf, static_cast<int>(sizeof(buf)));
    json::Reader r(buf, n);
    long v = 0;
    CHECK(r.get_int("addr", v));
    CHECK(v == 0x123456);  // the board's, not the 0xDD1234 that blob carried
    CHECK(r.get_int("addr_table", v));
    CHECK(v == 58);
}

// L, the migration: the two settings that left take their stored bytes with them.
TEST_CASE("settings: a blob written by version-6 firmware comes back without stealth or gyro") {
    struct V6 {
        uint8_t version{1};
        uint32_t device_addr{0};
        int16_t battery_offset_mv{0};
        int16_t freq_trim_e1_ppm{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        bool gyro_enabled{false};
        Units units{Units::Metric};
        char callsign[10]{0};
    };

    V6 old{};
    old.device_addr = 0x5B7E57;
    old.battery_offset_mv = -120;
    old.freq_trim_e1_ppm = -37;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.gyro_enabled = true;
    old.units = Units::Nautical;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 6;
    std::memcpy(blob + 1, &old, sizeof(V6));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V6));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V6) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V6) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.units == Units::Nautical);
    CHECK(std::string(out.callsign) == "D-KXYZ");

    // The layout it writes back is shorter by the two bytes those settings held.
    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    CHECK(blob_size() < 1 + sizeof(V6) + 4);
    Settings again;
    REQUIRE(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(std::string(again.callsign) == "D-KXYZ");
}

// J, the migration: a stored payload is routed by its version byte, never by its length.
TEST_CASE("settings: a blob written by version-3 firmware comes back as itself, untrimmed") {
    // The version-3 payload, byte for byte as that firmware memcpy'd its struct.
    struct V3 {
        uint8_t version{1};
        uint32_t device_addr{0};
        int16_t battery_offset_mv{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t page_mask{0x0F};
        char callsign[10]{0};
    };
    // Versions 1, 2, 3, 5 and 7 are all 28 bytes: nothing but the version byte parts them.
    CHECK(sizeof(V3) == 28u);

    V3 old{};
    old.device_addr = 0x5B7E57;
    old.battery_offset_mv = -120;  // a unit that WAS calibrated keeps its trim
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Nautical;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 3;
    std::memcpy(blob + 1, &old, sizeof(V3));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V3));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V3) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V3) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.units == Units::Nautical);
    CHECK(std::string(out.callsign) == "D-KXYZ");
    // A unit that stored its settings before the radio trim existed was never
    // measured, so it comes back on the reference its TCXO actually has: the
    // frequency it has been transmitting on all along.
    CHECK(int(out.freq_trim_e1_ppm) == 0);

    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    Settings again;
    REQUIRE(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(int(again.battery_offset_mv) == -120);
    CHECK(int(again.freq_trim_e1_ppm) == 0);
}

// K, the migration: version 6 is version 4's length again, and the version byte parts them.
TEST_CASE("settings: a blob written by version-4 firmware comes back without its page mask") {
    // The version-4 payload, byte for byte as that firmware memcpy'd its struct.
    struct V4 {
        uint8_t version{1};
        uint32_t device_addr{0};
        int16_t battery_offset_mv{0};
        int16_t freq_trim_e1_ppm{0};
        uint8_t addr_table{0};
        uint8_t aircraft_type{4};
        bool alarm_enabled{true};
        uint8_t alarm_volume{3};
        bool stealth{false};
        Units units{Units::Metric};
        uint8_t page_mask{0x0F};
        char callsign[10]{0};
    };

    V4 old{};
    old.device_addr = 0x5B7E57;
    old.battery_offset_mv = -120;
    old.freq_trim_e1_ppm = -37;
    old.addr_table = 6;
    old.aircraft_type = 9;
    old.alarm_enabled = false;
    old.alarm_volume = 5;
    old.stealth = true;
    old.units = Units::Nautical;
    old.page_mask = 0x05;
    std::memcpy(old.callsign, "D-KXYZ", 7);

    uint8_t blob[128] = {0};
    blob[0] = 4;
    std::memcpy(blob + 1, &old, sizeof(V4));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V4));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V4) + i] = static_cast<uint8_t>(crc >> (8 * i));

    Settings out;
    REQUIRE(from_blob(blob, 1 + sizeof(V4) + 4, out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -120);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
    CHECK(int(out.aircraft_type) == 9);
    CHECK_FALSE(out.alarm_enabled);
    CHECK(int(out.alarm_volume) == 5);
    CHECK(out.units == Units::Nautical);
    CHECK(std::string(out.callsign) == "D-KXYZ");

    uint8_t rewritten[128] = {0};
    to_blob(out, rewritten, sizeof(rewritten));
    CHECK(int(rewritten[0]) == int(kBlobVersion));
    Settings again;
    REQUIRE(from_blob(rewritten, blob_size(), again) == Status::Ok);
    CHECK(std::string(again.callsign) == "D-KXYZ");
}
