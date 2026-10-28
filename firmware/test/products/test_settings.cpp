// Settings survive a power cycle, so every way a stored blob can be wrong is a way
// the device can come up misconfigured: corrupted, written by an older firmware,
// or patched by a client sending a value out of range. Each one falls back or
// refuses whole. A partly applied patch is the outcome none of these allow.
//
// The address cases are what the device says it is: the prefix, then the serial the server signed.
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/fec/crc.h"
#include "core/power/battery.h"
#include "core/settings/address.h"
#include "core/util/json.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;
using namespace skyblip::settings;

TEST_CASE("settings: defaults are valid") {
    Settings settings = defaults();
    CHECK(validate(settings) == Status::Ok);
    CHECK(int(settings.aircraft_type) == kAircraftTypeLight);
}

TEST_CASE("settings: blob round-trips through version+crc framing") {
    Settings settings = defaults();
    settings.alarm_volume = AlarmVolume::Soft;
    settings.units = Units::Metric;
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(out.alarm_volume == AlarmVolume::Soft);
    CHECK(out.units == Units::Metric);
}

TEST_CASE("settings: the radar's range and plot are stored, and a new unit ships on 4 NM and ALL") {
    Settings settings = defaults();
    CHECK(int(settings.range_step) == kDefaultRangeStep);
    CHECK(settings.plot == RadarPlot::All);

    settings.range_step = 0;
    settings.plot = RadarPlot::ToScale;
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
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

// The radar's range and plot are set on the glass, and the config reply carries what a phone sets.
TEST_CASE(
    "settings: the range and the plot are the glass's, and the link neither shows nor sets them") {
    Settings settings = defaults();
    char buffer[256];
    const std::string json(
        buffer, static_cast<size_t>(
                    to_json(settings, settings::address_of(0x3456), buffer, sizeof(buffer))));
    CHECK(json.find("range") == std::string::npos);
    CHECK(json.find("plot") == std::string::npos);

    const char* patch = "{\"range_step\":0,\"plot\":1,\"alarm_volume\":1}";
    CHECK(apply_json(settings, patch, static_cast<int>(strlen(patch))) == Status::Ok);
    CHECK(int(settings.range_step) == kDefaultRangeStep);
    CHECK(settings.plot == RadarPlot::All);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);
}

TEST_CASE("settings: a corrupted blob is detected (CRC), caller falls back") {
    Settings settings = defaults();
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    blob[3] ^= 0xFF;  // flip a payload byte
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Crc);
}

TEST_CASE("settings: wrong version blob is Unsupported (migrate/default)") {
    Settings settings = defaults();
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    blob[0] = 99;  // bogus version
    // recompute crc so it isn't a CRC failure: simulate a real older version
    // (here we just confirm version mismatch is caught before trusting payload)
    Settings out;
    Status status = from_blob(blob, blob_size(), out);
    CHECK((status == Status::Unsupported || status == Status::Crc));
}

TEST_CASE("settings: to_json/apply_json round-trip of a patch") {
    Settings settings = defaults();
    char buffer[256];
    int written = to_json(settings, settings::address_of(0x3456), buffer, sizeof(buffer));
    CHECK(written > 0);
    json::Reader reader(buffer, written);
    long value;
    CHECK(reader.read_integer("aircraft_type", value));
    CHECK(value == kAircraftTypeLight);

    // apply a patch: change type + alarm volume + units
    const char* patch = "{\"aircraft_type\":4,\"alarm_volume\":0,\"units\":1}";
    CHECK(apply_json(settings, patch, static_cast<int>(strlen(patch))) == Status::Ok);
    CHECK(int(settings.aircraft_type) == 4);
    CHECK(settings.alarm_volume == AlarmVolume::Off);
    CHECK(settings.units == Units::Metric);
}

TEST_CASE("settings: apply_json rejects out-of-range atomically") {
    Settings settings = defaults();
    uint8_t before = settings.aircraft_type;
    const char* bad = "{\"aircraft_type\":99}";  // > 17
    CHECK(apply_json(settings, bad, static_cast<int>(strlen(bad))) == Status::OutOfRange);
    CHECK(settings.aircraft_type == before);  // unchanged (atomic)

    // The six-step scale an older companion sent tops out past loud.
    const char* six_step = "{\"alarm_volume\":3}";
    CHECK(apply_json(settings, six_step, static_cast<int>(strlen(six_step))) == Status::OutOfRange);
    CHECK(settings.alarm_volume == AlarmVolume::Loud);
}

// Issue #126: the registry assigned prefix 0x00, and the server fills the 16 bits below it.
TEST_CASE("address: the prefix, then the serial the server signed") {
    CHECK(address_of(0x0000) == 0x000000u);
    CHECK(address_of(0x012A) == 0x00012Au);
    CHECK(address_of(0xFFFF) == 0x00FFFFu);
    CHECK(int(kManufacturerPrefix) == 0x00);
}

// Issue 2 F.2.2 numbers manufacturer pages from 9, so page 49 is table entry 58.
TEST_CASE("address: the table is a constant, not a field anything can hold") {
    CHECK(int(kAddressMappingTable) == 58);
    char buffer[256];
    const int written =
        to_json(defaults(), address_of(0x0042), buffer, static_cast<int>(sizeof(buffer)));
    json::Reader reader(buffer, written);
    long value = 0;
    CHECK(reader.read_integer("address", value));
    CHECK(value == 0x000042);
    CHECK(reader.read_integer("address_mapping_table", value));
    CHECK(value == 58);
}

TEST_CASE("address: a unit with no serial reports no address, and still its table") {
    char buffer[256];
    const int written = to_json(defaults(), std::nullopt, buffer, static_cast<int>(sizeof(buffer)));
    json::Reader reader(buffer, written);
    long value = 0;
    CHECK_FALSE(reader.has("address"));
    CHECK(reader.read_integer("address_mapping_table", value));
    CHECK(value == 58);
}

// H. The per-unit gauge trim: one signed millivolt offset, set once on the line
// against a bench supply, bounded because a calibration field that accepts
// anything is a support incident of its own.
TEST_CASE("settings: the battery trim is bounded at the boundary, in both framings") {
    Settings settings = defaults();
    CHECK(int(settings.battery_offset_mv) == 0);  // an uncalibrated unit reads as it always did

    // The bound itself is inclusive, and one millivolt past it is not.
    settings.battery_offset_mv = power::kCalibrationCeilingMv;
    CHECK(validate(settings) == Status::Ok);
    settings.battery_offset_mv = -power::kCalibrationCeilingMv;
    CHECK(validate(settings) == Status::Ok);
    settings.battery_offset_mv = power::kCalibrationCeilingMv + 1;
    CHECK(validate(settings) == Status::OutOfRange);
    settings.battery_offset_mv = -(power::kCalibrationCeilingMv + 1);
    CHECK(validate(settings) == Status::OutOfRange);

    // A blob is the other way in, and it is validated on the way out of flash:
    // a unit whose stored trim is impossible falls back rather than reading its
    // cell through it.
    // Setting it is what makes it a hand-set trim, so the charger stops proposing.
    Settings by_hand = defaults();
    REQUIRE(apply_json(by_hand, "{\"battery_offset_mv\":-47}", 25) == Status::Ok);
    CHECK(by_hand.battery_offset_manual);
    CHECK(int(by_hand.battery_offset_mv) == -47);
    CHECK_FALSE(defaults().battery_offset_manual);

    settings.battery_offset_mv = -47;
    uint8_t blob[128] = {0};
    to_blob(settings, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.battery_offset_mv) == -47);

    Settings impossible = settings;
    impossible.battery_offset_mv = 3000;
    to_blob(impossible, blob, sizeof(blob));
    CHECK(from_blob(blob, blob_size(), out) == Status::Invalid);
}

TEST_CASE("settings: the battery trim is set over the link and refused whole when it is not") {
    Settings settings = defaults();
    const char* trim = "{\"battery_offset_mv\":-40}";
    CHECK(apply_json(settings, trim, static_cast<int>(strlen(trim))) == Status::Ok);
    CHECK(int(settings.battery_offset_mv) == -40);

    // Out of bound, and the rest of the patch does not land either.
    const char* far_out = "{\"alarm_volume\":1,\"battery_offset_mv\":900}";
    CHECK(apply_json(settings, far_out, static_cast<int>(strlen(far_out))) == Status::OutOfRange);
    CHECK(int(settings.battery_offset_mv) == -40);
    CHECK(settings.alarm_volume == AlarmVolume::Loud);

    // The boundary a narrowing cast would hide: 65536 truncates to 0 in an
    // int16, and 65576 truncates to 40 - both inside the bound, neither what
    // the client sent. The check has to happen before the narrowing.
    const char* wraps_to_zero = "{\"battery_offset_mv\":65536}";
    CHECK(apply_json(settings, wraps_to_zero, static_cast<int>(strlen(wraps_to_zero))) ==
          Status::OutOfRange);
    CHECK(int(settings.battery_offset_mv) == -40);
    const char* wraps_into_range = "{\"battery_offset_mv\":65576}";
    CHECK(apply_json(settings, wraps_into_range, static_cast<int>(strlen(wraps_into_range))) ==
          Status::OutOfRange);
    CHECK(int(settings.battery_offset_mv) == -40);
}

// J. The frequency trim: the field exists because a TCXO gives no way to find
// out it is wrong, so the bound is what says "out of trim" rather than "broken".
TEST_CASE("settings: the frequency trim is bounded at the boundary, in both framings") {
    Settings settings = defaults();
    CHECK(int(settings.frequency_trim_e1_ppm) == 0);  // the design intent on a TCXO part

    settings.frequency_trim_e1_ppm = kFrequencyTrimCeilingE1Ppm;
    CHECK(validate(settings) == Status::Ok);
    settings.frequency_trim_e1_ppm = -kFrequencyTrimCeilingE1Ppm;
    CHECK(validate(settings) == Status::Ok);
    settings.frequency_trim_e1_ppm = kFrequencyTrimCeilingE1Ppm + 1;
    CHECK(validate(settings) == Status::OutOfRange);
    settings.frequency_trim_e1_ppm = -(kFrequencyTrimCeilingE1Ppm + 1);
    CHECK(validate(settings) == Status::OutOfRange);

    // And it survives the flash framing, sign and all: a stored trim that came
    // back as its own negation would move the carrier twice as far the wrong way.
    settings.frequency_trim_e1_ppm = -37;
    REQUIRE(validate(settings) == Status::Ok);
    uint8_t blob[128] = {0};
    to_blob(settings, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(int(out.frequency_trim_e1_ppm) == -37);
}

TEST_CASE("settings: the frequency trim is set over the link and refused whole when it is not") {
    Settings settings = defaults();
    const char* set = "{\"frequency_trim_e1_ppm\":-25,\"alarm_volume\":1}";
    REQUIRE(apply_json(settings, set, static_cast<int>(strlen(set))) == Status::Ok);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    // Out of range is refused as a whole patch, not clamped and not partly
    // applied: a bench that asked for 500 ppm has the wrong number written down,
    // and silently storing 10 ppm instead would hide that.
    const char* far = "{\"frequency_trim_e1_ppm\":5000,\"alarm_volume\":2}";
    CHECK(apply_json(settings, far, static_cast<int>(strlen(far))) == Status::OutOfRange);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    // Narrowed before it is validated: 65536 tenths of a ppm truncates to 0 in
    // an int16 and would pass a bound check that never saw the value sent.
    const char* wrapped = "{\"frequency_trim_e1_ppm\":65536}";
    CHECK(apply_json(settings, wrapped, static_cast<int>(strlen(wrapped))) == Status::OutOfRange);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);

    // Write-only over the link, for the same reason battery_offset_mv is: the
    // "get" reply is one frame at comms::kSmallestSupportedPayloadBytes with 29
    // bytes of headroom, too few for both trims.
    char buffer[256];
    const int written = to_json(settings, settings::address_of(0x3456), buffer, sizeof(buffer));
    CHECK(std::string(buffer, static_cast<size_t>(written)).find("freq_trim") == std::string::npos);
}

TEST_CASE("settings: a blob from a version this firmware never wrote is refused") {
    Settings settings = defaults();
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    blob[0] = kBlobVersion + 1;
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Unsupported);
}

TEST_CASE("settings: the JSON offers nothing the firmware does not read") {
    Settings settings = defaults();
    char buffer[256];
    const int written = to_json(settings, settings::address_of(0x3456), buffer, sizeof(buffer));
    const std::string json(buffer, static_cast<size_t>(written));
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
    // one frame at comms::kSmallestSupportedPayloadBytes, its worst case is already
    // 173 of those 182 bytes, and no honest name for a signed millivolt trim
    // fits in nine. It is written on the line and read back as the millivolts
    // the status reply and the panel already show.
    CHECK(json.find("battery_offset_mv") == std::string::npos);
    const char* metric = "{\"units\":1}";
    CHECK(apply_json(settings, metric, static_cast<int>(strlen(metric))) == Status::Ok);
    CHECK(settings.units == Units::Metric);
    const char* nautical = "{\"units\":0}";
    CHECK(apply_json(settings, nautical, static_cast<int>(strlen(nautical))) == Status::Ok);
    CHECK(settings.units == Units::Nautical);

    // An older client still sending the dropped keys is not an error: the rest
    // of its patch applies, and the keys nothing reads are ignored.
    const char* stale = "{\"region\":3,\"rotation\":2,\"power_save\":true,\"alarm_volume\":1}";
    CHECK(apply_json(settings, stale, static_cast<int>(strlen(stale))) == Status::Ok);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);
}

TEST_CASE("settings: a callsign is what a panel can draw, and a patch that is not is refused") {
    Settings settings = defaults();
    const char* ok = "{\"callsign\":\"G-ABCD\"}";
    CHECK(apply_json(settings, ok, static_cast<int>(strlen(ok))) == Status::Ok);
    CHECK(std::string(settings.callsign) == "G-ABCD");

    Settings bad = settings;
    bad.callsign[2] = 0x07;  // a control character the 5x7 font has no glyph for
    CHECK(validate(bad) == Status::Invalid);

    // Nine characters plus the terminator is the whole field, and it survives.
    const char* full = "{\"callsign\":\"123456789\"}";
    CHECK(apply_json(settings, full, static_cast<int>(strlen(full))) == Status::Ok);
    CHECK(std::string(settings.callsign) == "123456789");
}

TEST_CASE("settings: a callsign longer than the field is refused, not cut to fit") {
    Settings settings = defaults();
    const char* ten = "{\"callsign\":\"1234567890\"}";
    CHECK(apply_json(settings, ten, static_cast<int>(strlen(ten))) == Status::OutOfRange);
    CHECK(std::string(settings.callsign).empty());
}

TEST_CASE("settings: a callsign is only what the menu can type, so a comma or a star is refused") {
    for (const char* patch :
         {"{\"callsign\":\"D,KXYZ\"}", "{\"callsign\":\"D*KXYZ\"}", "{\"callsign\":\"$DKXYZ\"}",
          "{\"callsign\":\"D!KXYZ\"}", "{\"callsign\":\"d-kxyz\"}"}) {
        CAPTURE(patch);
        Settings settings = defaults();
        CHECK(apply_json(settings, patch, static_cast<int>(strlen(patch))) == Status::Invalid);
        CHECK(std::string(settings.callsign).empty());
    }
}

// Older builds took any printable name over the link, and refusing the whole blob lost the trims.
TEST_CASE("settings: a stored name the menu cannot type is dropped, and the trims beside it kept") {
    Settings settings = defaults();
    settings.battery_offset_mv = 120;
    settings.frequency_trim_e1_ppm = -35;
    std::strncpy(settings.callsign, "d-kxyz", kCallsignBytes - 1);
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    Settings out;
    REQUIRE(from_blob(blob, blob_size(), out) == Status::Ok);
    CHECK(std::string(out.callsign).empty());
    CHECK(out.battery_offset_mv == 120);
    CHECK(out.frequency_trim_e1_ppm == -35);
}

TEST_CASE("settings: a type or a volume is refused before it is narrowed, never stored wrapped") {
    for (const char* patch : {"{\"aircraft_type\":260}", "{\"aircraft_type\":-252}",
                              "{\"alarm_volume\":256}", "{\"alarm_volume\":-253}"}) {
        CAPTURE(patch);
        Settings settings = defaults();
        CHECK(apply_json(settings, patch, static_cast<int>(strlen(patch))) == Status::OutOfRange);
        CHECK(int(settings.aircraft_type) == int(defaults().aircraft_type));
        CHECK(int(settings.alarm_volume) == int(defaults().alarm_volume));
    }
}

TEST_CASE("settings: a patch that names an identity changes nothing and refuses nothing") {
    Settings settings = defaults();
    const char* icao =
        "{\"address\":14488116,\"address_mapping_table\":5,\"alarm_volume\":1}";  // 0xDD1234
    CHECK(apply_json(settings, icao, static_cast<int>(strlen(icao))) == Status::Ok);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    char buffer[256];
    const int written = to_json(settings, 0x5B7E57, buffer, static_cast<int>(sizeof(buffer)));
    json::Reader reader(buffer, written);
    long value = 0;
    CHECK(reader.read_integer("address", value));
    CHECK(value == 0x5B7E57);
    CHECK(reader.read_integer("address_mapping_table", value));
    CHECK(value == 58);
}
