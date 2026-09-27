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

TEST_CASE("settings: the radar's range and plot are stored, and a new unit ships on 4 NM and ALL") {
    Settings s = defaults();
    CHECK(int(s.range_step) == kDefaultRangeStep);
    CHECK(s.plot == RadarPlot::All);

    s.range_step = 0;
    s.plot = RadarPlot::ToScale;
    uint8_t blob[128];
    to_blob(s, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.range_step) == 0);
    CHECK(out.plot == RadarPlot::ToScale);

    Settings past_the_last_ring = defaults();
    past_the_last_ring.range_step = kRangeStepCount;
    CHECK(validate(past_the_last_ring) == Status::OutOfRange);
    Settings no_such_plot = defaults();
    no_such_plot.plot = static_cast<RadarPlot>(2);
    CHECK(validate(no_such_plot) == Status::OutOfRange);
}

// The "config" reply has nine bytes left at its worst case, and neither key fits in them.
TEST_CASE(
    "settings: the range and the plot are the glass's, and the link neither shows nor sets them") {
    Settings s = defaults();
    char buf[256];
    const std::string json(buf, static_cast<size_t>(to_json(s, 0x123456, buf, sizeof(buf))));
    CHECK(json.find("range") == std::string::npos);
    CHECK(json.find("plot") == std::string::npos);

    const char* patch = "{\"range_step\":0,\"plot\":1,\"alarm_volume\":2}";
    CHECK(apply_json(s, patch, static_cast<int>(strlen(patch))) == Status::Ok);
    CHECK(int(s.range_step) == kDefaultRangeStep);
    CHECK(s.plot == RadarPlot::All);
    CHECK(int(s.alarm_volume) == 2);
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
