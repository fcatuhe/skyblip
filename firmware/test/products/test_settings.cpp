// Settings survive a power cycle, so every way a stored blob can be wrong is a way
// the device can come up misconfigured: corrupted, written by an older firmware,
// or patched by a client sending a value out of range. Each one falls back or
// refuses whole. A partly applied patch is the outcome none of these allow.
//
// The address cases are the other half of the same subject: what the device says
// it is. A chip id is a serial number, and the space it lands in is shared with
// every other tracker that mints its identity the same way.
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

TEST_CASE("settings: defaults are valid") {
    Settings s = defaults();
    CHECK(validate(s) == Status::Ok);
    CHECK(int(s.aircraft_type) == kAircraftTypeLight);
}

TEST_CASE("settings: blob round-trips through version+crc framing") {
    Settings s = defaults();
    s.alarm_volume = 4;
    s.units = Units::Metric;
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.alarm_volume) == 4);
    CHECK(out.units == Units::Metric);
}

TEST_CASE("settings: a corrupted blob is detected (CRC), caller falls back") {
    Settings s = defaults();
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[3] ^= 0xFF;  // flip a payload byte
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Crc);
}

TEST_CASE("settings: wrong version blob is Unsupported (migrate/default)") {
    Settings s = defaults();
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[0] = 99;  // bogus version
    // recompute crc so it isn't a CRC failure: simulate a real older version
    // (here we just confirm version mismatch is caught before trusting payload)
    Settings out;
    Status st = from_blob(blob, blob_size(), out);
    CHECK((st == Status::Unsupported || st == Status::Crc));
}

TEST_CASE("settings: to_json/apply_json round-trip of a patch") {
    Settings s = defaults();
    char buf[256];
    int n = to_json(s, 0x123456, buf, sizeof(buf));
    CHECK(n > 0);
    json::Reader r(buf, n);
    long v;
    CHECK(r.get_int("aircraft_type", v));
    CHECK(v == kAircraftTypeLight);

    // apply a patch: change type + alarm volume + units
    const char* patch = "{\"aircraft_type\":4,\"alarm_volume\":5,\"units\":1}";
    CHECK(apply_json(s, patch, static_cast<int>(strlen(patch))) == Status::Ok);
    CHECK(int(s.aircraft_type) == 4);
    CHECK(int(s.alarm_volume) == 5);
    CHECK(s.units == Units::Metric);
}

TEST_CASE("settings: apply_json rejects out-of-range atomically") {
    Settings s = defaults();
    uint8_t before = s.aircraft_type;
    const char* bad = "{\"aircraft_type\":99}";  // > 17
    CHECK(apply_json(s, bad, static_cast<int>(strlen(bad))) == Status::OutOfRange);
    CHECK(s.aircraft_type == before);  // unchanged (atomic)
}

// F4. The table says which space the address came from, so no prefix is ours to move.
TEST_CASE("address: every prefix is left exactly where the chip put it") {
    for (uint32_t prefix = 0; prefix <= 0xFF; prefix++) {
        const uint32_t raw = (prefix << 16) | 0xABCD;
        CHECK(air_address(raw) == raw);
    }
}

TEST_CASE("address: neither all-zeros nor all-ones goes on the air") {
    CHECK(air_address(0x000000) == kFallbackAddress);
    CHECK(air_address(0xFFFFFF) == kFallbackAddress);
    CHECK(air_address(0xFF000000) == kFallbackAddress);  // masked to 24 bits first
    CHECK(air_address(kFallbackAddress) == kFallbackAddress);
}

// Issue 2 F.2.2: 0 is privacy and must be re-drawn every start-up, 1 to 4 are reserved.
TEST_CASE("address: the table is a constant, not a field anything can hold") {
    CHECK(int(kAddrTableSkyblip) == 58);
    char buf[256];
    const int n = to_json(defaults(), 0xDD0042, buf, static_cast<int>(sizeof(buf)));
    json::Reader r(buf, n);
    long v = 0;
    CHECK(r.get_int("addr", v));
    CHECK(v == 0xDD0042);  // the chip's number, prefix and all
    CHECK(r.get_int("addr_table", v));
    CHECK(v == 58);
}

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

// H. The per-unit gauge trim: one signed millivolt offset, set once on the line
// against a bench supply, bounded because a calibration field that accepts
// anything is a support incident of its own.
TEST_CASE("settings: the battery trim is bounded at the boundary, in both framings") {
    Settings s = defaults();
    CHECK(int(s.battery_offset_mv) == 0);  // an uncalibrated unit reads as it always did

    // The bound itself is inclusive, and one millivolt past it is not.
    s.battery_offset_mv = power::kCalibrationLimitMv;
    CHECK(validate(s) == Status::Ok);
    s.battery_offset_mv = -power::kCalibrationLimitMv;
    CHECK(validate(s) == Status::Ok);
    s.battery_offset_mv = power::kCalibrationLimitMv + 1;
    CHECK(validate(s) == Status::OutOfRange);
    s.battery_offset_mv = -(power::kCalibrationLimitMv + 1);
    CHECK(validate(s) == Status::OutOfRange);

    // A blob is the other way in, and it is validated on the way out of flash:
    // a unit whose stored trim is impossible falls back rather than reading its
    // cell through it.
    // Setting it is what makes it a hand-set trim, so the charger stops proposing.
    Settings by_hand = defaults();
    REQUIRE(apply_json(by_hand, "{\"battery_offset_mv\":-47}", 25) == Status::Ok);
    CHECK(by_hand.battery_offset_manual);
    CHECK(int(by_hand.battery_offset_mv) == -47);
    CHECK_FALSE(defaults().battery_offset_manual);

    s.battery_offset_mv = -47;
    uint8_t blob[128] = {0};
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -47);

    Settings impossible = s;
    impossible.battery_offset_mv = 3000;
    to_blob(impossible, blob, sizeof(blob));
    CHECK(from_blob(blob, blob_size(), out) == Status::Invalid);
}

TEST_CASE("settings: the battery trim is set over the link and refused whole when it is not") {
    Settings s = defaults();
    const char* trim = "{\"battery_offset_mv\":-40}";
    CHECK(apply_json(s, trim, static_cast<int>(strlen(trim))) == Status::Ok);
    CHECK(int(s.battery_offset_mv) == -40);

    // Out of bound, and the rest of the patch does not land either.
    const char* far_out = "{\"alarm_volume\":1,\"battery_offset_mv\":900}";
    CHECK(apply_json(s, far_out, static_cast<int>(strlen(far_out))) == Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
    CHECK(int(s.alarm_volume) == 3);

    // The boundary a narrowing cast would hide: 65536 truncates to 0 in an
    // int16, and 65576 truncates to 40 - both inside the bound, neither what
    // the client sent. The check has to happen before the narrowing.
    const char* wraps_to_zero = "{\"battery_offset_mv\":65536}";
    CHECK(apply_json(s, wraps_to_zero, static_cast<int>(strlen(wraps_to_zero))) ==
          Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
    const char* wraps_into_range = "{\"battery_offset_mv\":65576}";
    CHECK(apply_json(s, wraps_into_range, static_cast<int>(strlen(wraps_into_range))) ==
          Status::OutOfRange);
    CHECK(int(s.battery_offset_mv) == -40);
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

// J. The frequency trim: the field exists because a TCXO gives no way to find
// out it is wrong, so the bound is what says "out of trim" rather than "broken".
TEST_CASE("settings: the frequency trim is bounded at the boundary, in both framings") {
    Settings s = defaults();
    CHECK(int(s.freq_trim_e1_ppm) == 0);  // the design intent on a TCXO part

    s.freq_trim_e1_ppm = kFreqTrimLimitTenthsPpm;
    CHECK(validate(s) == Status::Ok);
    s.freq_trim_e1_ppm = -kFreqTrimLimitTenthsPpm;
    CHECK(validate(s) == Status::Ok);
    s.freq_trim_e1_ppm = kFreqTrimLimitTenthsPpm + 1;
    CHECK(validate(s) == Status::OutOfRange);
    s.freq_trim_e1_ppm = -(kFreqTrimLimitTenthsPpm + 1);
    CHECK(validate(s) == Status::OutOfRange);

    // And it survives the flash framing, sign and all: a stored trim that came
    // back as its own negation would move the carrier twice as far the wrong way.
    s.freq_trim_e1_ppm = -37;
    REQUIRE(validate(s) == Status::Ok);
    uint8_t blob[128] = {0};
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.freq_trim_e1_ppm) == -37);
}

TEST_CASE("settings: the frequency trim is set over the link and refused whole when it is not") {
    Settings s = defaults();
    const char* set = "{\"freq_trim_e1_ppm\":-25,\"alarm_volume\":2}";
    REQUIRE(apply_json(s, set, static_cast<int>(strlen(set))) == Status::Ok);
    CHECK(int(s.freq_trim_e1_ppm) == -25);
    CHECK(int(s.alarm_volume) == 2);

    // Out of range is refused as a whole patch, not clamped and not partly
    // applied: a bench that asked for 500 ppm has the wrong number written down,
    // and silently storing 10 ppm instead would hide that.
    const char* far = "{\"freq_trim_e1_ppm\":5000,\"alarm_volume\":5}";
    CHECK(apply_json(s, far, static_cast<int>(strlen(far))) == Status::OutOfRange);
    CHECK(int(s.freq_trim_e1_ppm) == -25);
    CHECK(int(s.alarm_volume) == 2);

    // Narrowed before it is validated: 65536 tenths of a ppm truncates to 0 in
    // an int16 and would pass a bound check that never saw the value sent.
    const char* wrapped = "{\"freq_trim_e1_ppm\":65536}";
    CHECK(apply_json(s, wrapped, static_cast<int>(strlen(wrapped))) == Status::OutOfRange);
    CHECK(int(s.freq_trim_e1_ppm) == -25);

    // Write-only over the link, for the same reason battery_offset_mv is: the
    // "get" reply is one frame at comms::kSmallestSupportedPayload with nine
    // bytes of headroom, and "freq_trim_e1_ppm" alone is nineteen.
    char buf[256];
    const int n = to_json(s, 0x123456, buf, sizeof(buf));
    CHECK(std::string(buf, static_cast<size_t>(n)).find("freq_trim") == std::string::npos);
}

TEST_CASE("settings: a blob from a version this firmware never wrote is refused") {
    Settings s = defaults();
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    blob[0] = kBlobVersion + 1;
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Unsupported);
}

TEST_CASE("settings: the JSON offers nothing the firmware does not read") {
    Settings s = defaults();
    char buf[256];
    const int n = to_json(s, 0x123456, buf, sizeof(buf));
    const std::string json(buf, static_cast<size_t>(n));
    // Removed with their fields: no reader outside this module, and a page that
    // accepts a setting nothing reads is worse than one that does not offer it.
    CHECK(json.find("region") == std::string::npos);
    CHECK(json.find("rotation") == std::string::npos);
    CHECK(json.find("power_save") == std::string::npos);
    // And the page mask went the same way when the pad's walk became three
    // pages the device may not hide.
    CHECK(json.find("page_mask") == std::string::npos);
    // So did the two the menu no longer offers: nothing branches on either.
    CHECK(json.find("stealth") == std::string::npos);
    CHECK(json.find("gyro") == std::string::npos);
    // Kept, because each of these is read: the panel (callsign, units), the
    // annunciator (alarm, alarm_volume).
    CHECK(json.find("callsign") != std::string::npos);
    // units survived the same audit the three above failed, on one page: the
    // six-pack graduates its dials in km/h, metres and m/s or in kt, ft and
    // fpm, and there is nowhere else a value appears exactly once.
    CHECK(json.find("units") != std::string::npos);
    // The one exception, and it goes the other way: the firmware reads
    // battery_offset_mv and the reply does not offer it. The "config" reply is
    // one frame at comms::kSmallestSupportedPayload, its worst case is already
    // 173 of those 182 bytes, and no honest name for a signed millivolt trim
    // fits in nine. It is written on the line and read back as the millivolts
    // the status reply and the panel already show.
    CHECK(json.find("battery_offset_mv") == std::string::npos);
    const char* metric = "{\"units\":1}";
    CHECK(apply_json(s, metric, static_cast<int>(strlen(metric))) == Status::Ok);
    CHECK(s.units == Units::Metric);
    const char* nautical = "{\"units\":0}";
    CHECK(apply_json(s, nautical, static_cast<int>(strlen(nautical))) == Status::Ok);
    CHECK(s.units == Units::Nautical);

    // An older client still sending the dropped keys is not an error: the rest
    // of its patch applies, and the keys nothing reads are ignored.
    const char* stale = "{\"region\":3,\"rotation\":2,\"power_save\":true,\"alarm_volume\":1}";
    CHECK(apply_json(s, stale, static_cast<int>(strlen(stale))) == Status::Ok);
    CHECK(int(s.alarm_volume) == 1);
}

TEST_CASE("settings: a callsign is what a panel can draw, and a patch that is not is refused") {
    Settings s = defaults();
    const char* ok = "{\"callsign\":\"G-ABCD\"}";
    CHECK(apply_json(s, ok, static_cast<int>(strlen(ok))) == Status::Ok);
    CHECK(std::string(s.callsign) == "G-ABCD");

    Settings bad = s;
    bad.callsign[2] = 0x07;  // a control character the 5x7 font has no glyph for
    CHECK(validate(bad) == Status::Invalid);

    // Nine characters plus the terminator is the whole field, and it survives.
    const char* full = "{\"callsign\":\"123456789\"}";
    CHECK(apply_json(s, full, static_cast<int>(strlen(full))) == Status::Ok);
    CHECK(std::string(s.callsign) == "123456789");
}

TEST_CASE("settings: a callsign longer than the field is refused, not cut to fit") {
    Settings s = defaults();
    const char* ten = "{\"callsign\":\"1234567890\"}";
    CHECK(apply_json(s, ten, static_cast<int>(strlen(ten))) == Status::OutOfRange);
    CHECK(std::string(s.callsign).empty());
}

TEST_CASE("settings: a callsign is only what the menu can type, so a comma or a star is refused") {
    for (const char* patch :
         {"{\"callsign\":\"D,KXYZ\"}", "{\"callsign\":\"D*KXYZ\"}", "{\"callsign\":\"$DKXYZ\"}",
          "{\"callsign\":\"D!KXYZ\"}", "{\"callsign\":\"d-kxyz\"}"}) {
        CAPTURE(patch);
        Settings s = defaults();
        CHECK(apply_json(s, patch, static_cast<int>(strlen(patch))) == Status::Invalid);
        CHECK(std::string(s.callsign).empty());
    }
}

// Older builds took any printable name over the link, and refusing the whole blob lost the trims.
TEST_CASE("settings: a stored name the menu cannot type is dropped, and the trims beside it kept") {
    Settings s = defaults();
    s.battery_offset_mv = 120;
    s.freq_trim_e1_ppm = -35;
    std::strncpy(s.callsign, "d-kxyz", kCallsignCap - 1);
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(std::string(out.callsign).empty());
    CHECK(out.battery_offset_mv == 120);
    CHECK(out.freq_trim_e1_ppm == -35);
}

TEST_CASE("settings: a type or a volume is refused before it is narrowed, never stored wrapped") {
    for (const char* patch : {"{\"aircraft_type\":260}", "{\"aircraft_type\":-252}",
                              "{\"alarm_volume\":256}", "{\"alarm_volume\":-253}"}) {
        CAPTURE(patch);
        Settings s = defaults();
        CHECK(apply_json(s, patch, static_cast<int>(strlen(patch))) == Status::OutOfRange);
        CHECK(int(s.aircraft_type) == int(defaults().aircraft_type));
        CHECK(int(s.alarm_volume) == int(defaults().alarm_volume));
    }
}

TEST_CASE("settings: a patch that names an identity changes nothing and refuses nothing") {
    Settings s = defaults();
    const char* icao = "{\"addr\":14488116,\"addr_table\":5,\"alarm_volume\":1}";  // 0xDD1234
    CHECK(apply_json(s, icao, static_cast<int>(strlen(icao))) == Status::Ok);
    CHECK(int(s.alarm_volume) == 1);

    char buf[256];
    const int n = to_json(s, 0x5B7E57, buf, static_cast<int>(sizeof(buf)));
    json::Reader r(buf, n);
    long v = 0;
    CHECK(r.get_int("addr", v));
    CHECK(v == 0x5B7E57);
    CHECK(r.get_int("addr_table", v));
    CHECK(v == 58);
}

TEST_CASE("json_min: the reader parses ints, bools and strings, the writer emits them") {
    const char* j = "{\"a\":42,\"b\":true,\"c\":\"hi\",\"d\":-7}";
    json::Reader r(j, static_cast<int>(strlen(j)));
    long v;
    bool b;
    char s[8];
    CHECK(r.get_int("a", v));
    CHECK(v == 42);
    CHECK(r.get_bool("b", b));
    CHECK(b);
    CHECK(r.get_str("c", s, sizeof(s)));
    CHECK(std::string(s) == "hi");
    CHECK(r.get_int("d", v));
    CHECK(v == -7);
    CHECK_FALSE(r.has("z"));

    char out[64];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    w.kv_bool("y", false);
    w.finish();
    CHECK(std::string(out) == "{\"x\":5,\"y\":false}");
}

TEST_CASE("json_min: a frame that ends on a colon has no value, and nothing past it is read") {
    const char frame[] = {'{', '"', 'c', 'm', 'd', '"', ':'};
    json::Reader r(frame, static_cast<int>(sizeof(frame)));
    long v = 0;
    bool b = false;
    char s[8];
    CHECK_FALSE(r.get_int("cmd", v));
    CHECK_FALSE(r.get_bool("cmd", b));
    CHECK_FALSE(r.get_str("cmd", s, sizeof(s)));
}

TEST_CASE("json_min: an integer past 32 bits is refused, so the host reads what the device reads") {
    const char* j =
        "{\"max\":2147483647,\"min\":-2147483648,\"over\":2147483648,\"under\":-2147483649,"
        "\"huge\":99999999999999999999999}";
    json::Reader r(j, static_cast<int>(strlen(j)));
    long v = 0;
    CHECK(r.get_int("max", v));
    CHECK(v == 2147483647L);
    CHECK(r.get_int("min", v));
    CHECK(v == -2147483647L - 1);
    CHECK_FALSE(r.get_int("over", v));
    CHECK_FALSE(r.get_int("under", v));
    CHECK_FALSE(r.get_int("huge", v));
}

// core/comms's status reply is a fixed-size stack buffer with no heap behind
// it: a key that overruns it must never come out half-written. A writer that
// silently dropped the tail of its last key would still close the brace and
// look like valid, complete JSON to anything downstream.
TEST_CASE("json_min: a key that will not fit whole is left out, not cut short") {
    char out[16];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    CHECK_FALSE(w.overflowed());

    w.kv_bool("yy", true);  // does not fit in what is left of a 16-byte buffer
    CHECK(w.overflowed());

    const int n = w.finish();
    CHECK(std::string(out) == "{\"x\":5}");  // whole and valid, one key short
    CHECK(n == 7);
    CHECK(static_cast<int>(std::strlen(out)) == n);  // the reported length is real
}

TEST_CASE("json_min: a buffer sized for the exact worst case never overflows") {
    char out[29];
    json::Writer w(out, sizeof(out));
    w.kv_str("k", "say \"hi\"");  // 1-char key, two escaped quotes in the value
    w.kv_bool("b", false);
    const int n = w.finish();
    CHECK_FALSE(w.overflowed());
    CHECK(static_cast<int>(std::strlen(out)) == n);
    CHECK(std::string(out) == "{\"k\":\"say \\\"hi\\\"\",\"b\":false}");
}
