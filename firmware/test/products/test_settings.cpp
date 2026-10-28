// Settings survive a power cycle, so every way a stored blob can be wrong is a way
// the device can come up misconfigured: corrupted, written by an older firmware,
// or patched by a client sending a value out of range. Each one falls back or
// refuses whole. A partly applied patch is the outcome none of these allow.
//
// The address cases are what the device says it is: the prefix, then the serial the server signed.
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/comms/link_registry.h"
#include "core/fec/crc.h"
#include "core/power/battery.h"
#include "core/settings/address.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"
#include "simulator/world/link_names.h"
#include "test/support/settings_link.h"

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
    const std::string json = config_fields(settings, settings::address_of(0x3456));
    CHECK(json.find("range") == std::string::npos);
    CHECK(json.find("plot") == std::string::npos);

    int set_frames = 0;
    for (const simulator::link_names::Frame& frame : simulator::link_names::kFrames) {
        if (std::strcmp(frame.command, "set") != 0) continue;
        set_frames++;
        for (int i = 0; i < frame.count; i++) {
            CHECK(std::string(frame.keys[i].name).find("range") == std::string::npos);
            CHECK(std::string(frame.keys[i].name).find("plot") == std::string::npos);
        }
    }
    CHECK(set_frames == 1);
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

TEST_CASE("settings: the config fields and a set patch round-trip over the link") {
    Settings settings = defaults();
    CHECK(config_fields(settings, settings::address_of(0x3456)).find("\"aircraft_type\":1,") !=
          std::string::npos);

    // apply a patch: change type + alarm volume + units
    const char* patch = "{\"aircraft_type\":4,\"alarm_volume\":0,\"units\":1}";
    CHECK(apply_patch(settings, patch) == Status::Ok);
    CHECK(int(settings.aircraft_type) == 4);
    CHECK(settings.alarm_volume == AlarmVolume::Off);
    CHECK(settings.units == Units::Metric);
}

TEST_CASE("settings: a set patch out of range is refused atomically") {
    Settings settings = defaults();
    uint8_t before = settings.aircraft_type;
    const char* bad = "{\"aircraft_type\":99}";  // > 17
    CHECK(apply_patch(settings, bad) == Status::OutOfRange);
    CHECK(settings.aircraft_type == before);  // unchanged (atomic)

    // The six-step scale an older companion sent tops out past loud.
    const char* six_step = "{\"alarm_volume\":3}";
    CHECK(apply_patch(settings, six_step) == Status::OutOfRange);
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
    const std::string json = config_fields(defaults(), address_of(0x0042));
    CHECK(json.find("\"address\":66,") != std::string::npos);
    CHECK(json.find("\"address_mapping_table\":58,") != std::string::npos);
}

TEST_CASE("address: a unit with no serial reports no address, and still its table") {
    const std::string json = config_fields(defaults(), std::nullopt);
    CHECK(json.find("\"address\"") == std::string::npos);
    CHECK(json.find("\"address_mapping_table\":58,") != std::string::npos);
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
    REQUIRE(apply_patch(by_hand, "{\"battery_offset_mv\":-47}") == Status::Ok);
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
    CHECK(apply_patch(settings, trim) == Status::Ok);
    CHECK(int(settings.battery_offset_mv) == -40);

    // Out of bound, and the rest of the patch does not land either.
    const char* far_out = "{\"alarm_volume\":1,\"battery_offset_mv\":900}";
    CHECK(apply_patch(settings, far_out) == Status::OutOfRange);
    CHECK(int(settings.battery_offset_mv) == -40);
    CHECK(settings.alarm_volume == AlarmVolume::Loud);

    // The boundary a narrowing cast would hide: 65536 truncates to 0 in an
    // int16, and 65576 truncates to 40 - both inside the bound, neither what
    // the client sent. The check has to happen before the narrowing.
    const char* wraps_to_zero = "{\"battery_offset_mv\":65536}";
    CHECK(apply_patch(settings, wraps_to_zero) == Status::OutOfRange);
    CHECK(int(settings.battery_offset_mv) == -40);
    const char* wraps_into_range = "{\"battery_offset_mv\":65576}";
    CHECK(apply_patch(settings, wraps_into_range) == Status::OutOfRange);
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
    REQUIRE(apply_patch(settings, set) == Status::Ok);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    // Out of range is refused as a whole patch, not clamped and not partly
    // applied: a bench that asked for 500 ppm has the wrong number written down,
    // and silently storing 10 ppm instead would hide that.
    const char* far = "{\"frequency_trim_e1_ppm\":5000,\"alarm_volume\":2}";
    CHECK(apply_patch(settings, far) == Status::OutOfRange);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    // Narrowed before it is validated: 65536 tenths of a ppm truncates to 0 in
    // an int16 and would pass a bound check that never saw the value sent.
    const char* wrapped = "{\"frequency_trim_e1_ppm\":65536}";
    CHECK(apply_patch(settings, wrapped) == Status::OutOfRange);
    CHECK(int(settings.frequency_trim_e1_ppm) == -25);

    // Write-only over the link, as battery_offset_mv is.
    CHECK(config_fields(settings, settings::address_of(0x3456)).find("frequency_trim") ==
          std::string::npos);
}

TEST_CASE("settings: a blob from a version this firmware never wrote is refused") {
    Settings settings = defaults();
    uint8_t blob[128];
    to_blob(settings, blob, sizeof(blob));
    blob[0] = kBlobVersion + 1;
    Settings out;
    CHECK(from_blob(blob, blob_size(), out) == Status::Unsupported);
}

TEST_CASE("settings: the config reply offers nothing the firmware does not read") {
    Settings settings = defaults();
    const std::string json = config_fields(settings, settings::address_of(0x3456));
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
    // The exception the other way: the firmware reads battery_offset_mv, the reply omits it.
    CHECK(json.find("battery_offset_mv") == std::string::npos);
    const char* metric = "{\"units\":1}";
    CHECK(apply_patch(settings, metric) == Status::Ok);
    CHECK(settings.units == Units::Metric);
    const char* nautical = "{\"units\":0}";
    CHECK(apply_patch(settings, nautical) == Status::Ok);
    CHECK(settings.units == Units::Nautical);

    // Keys nothing reads are ignored, and the rest of the patch applies.
    uint8_t stale[16];
    cbor::Writer writer(stale, sizeof(stale));
    writer.add_code(link::kCommandKey, link::Command::Set);
    writer.add_unsigned(link::set_key::kAlarmVolume, 1);
    writer.add_unsigned(40, 3);
    writer.add_boolean(41, true);
    const int stale_bytes = writer.finish();
    CHECK(apply_settings(settings, cbor::Reader(ConstByteSpan(
                                       stale, static_cast<size_t>(stale_bytes)))) == Status::Ok);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);
}

TEST_CASE("settings: a callsign is what a panel can draw, and a patch that is not is refused") {
    Settings settings = defaults();
    const char* ok = "{\"callsign\":\"G-ABCD\"}";
    CHECK(apply_patch(settings, ok) == Status::Ok);
    CHECK(std::string(settings.callsign) == "G-ABCD");

    Settings bad = settings;
    bad.callsign[2] = 0x07;  // a control character the 5x7 font has no glyph for
    CHECK(validate(bad) == Status::Invalid);

    // Nine characters plus the terminator is the whole field, and it survives.
    const char* full = "{\"callsign\":\"123456789\"}";
    CHECK(apply_patch(settings, full) == Status::Ok);
    CHECK(std::string(settings.callsign) == "123456789");
}

TEST_CASE("settings: a callsign longer than the field is refused, not cut to fit") {
    Settings settings = defaults();
    const char* ten = "{\"callsign\":\"1234567890\"}";
    CHECK(apply_patch(settings, ten) == Status::OutOfRange);
    CHECK(std::string(settings.callsign).empty());
}

TEST_CASE("settings: a callsign is only what the menu can type, so a comma or a star is refused") {
    for (const char* patch :
         {"{\"callsign\":\"D,KXYZ\"}", "{\"callsign\":\"D*KXYZ\"}", "{\"callsign\":\"$DKXYZ\"}",
          "{\"callsign\":\"D!KXYZ\"}", "{\"callsign\":\"d-kxyz\"}"}) {
        CAPTURE(patch);
        Settings settings = defaults();
        CHECK(apply_patch(settings, patch) == Status::Invalid);
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
    for (const char* patch : {"{\"aircraft_type\":260}", "{\"alarm_volume\":256}"}) {
        CAPTURE(patch);
        Settings settings = defaults();
        CHECK(apply_patch(settings, patch) == Status::OutOfRange);
        CHECK(int(settings.aircraft_type) == int(defaults().aircraft_type));
        CHECK(int(settings.alarm_volume) == int(defaults().alarm_volume));
    }
    for (const char* patch : {"{\"aircraft_type\":-252}", "{\"alarm_volume\":-253}"}) {
        CAPTURE(patch);
        Settings settings = defaults();
        CHECK(apply_patch(settings, patch) == Status::Invalid);
        CHECK(int(settings.aircraft_type) == int(defaults().aircraft_type));
        CHECK(int(settings.alarm_volume) == int(defaults().alarm_volume));
    }
}

TEST_CASE("settings: a patch that names an identity changes nothing and refuses nothing") {
    Settings settings = defaults();
    const char* icao =
        "{\"address\":14488116,\"address_mapping_table\":5,\"alarm_volume\":1}";  // 0xDD1234
    CHECK(apply_patch(settings, icao) == Status::Ok);
    CHECK(settings.alarm_volume == AlarmVolume::Soft);

    const std::string json = config_fields(settings, 0x5B7E57);
    CHECK(json.find("\"address\":5996119,") != std::string::npos);
    CHECK(json.find("\"address_mapping_table\":58,") != std::string::npos);
}
