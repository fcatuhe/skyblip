// The one alarm setting, version 14: off, visual, soft and loud, and what a Go that cannot sound
// keeps.
#include <cstring>
#include <string>

#include "core/fec/crc.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

struct V13 {
    uint8_t version{1};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    bool battery_offset_manual{false};
    uint8_t aircraft_type{1};
    bool alarm_enabled{true};
    uint8_t alarm_volume{2};
    Units units{Units::Nautical};
    uint8_t range_step{2};
    RadarPlot plot{RadarPlot::All};
    char callsign[10]{0};
    uint32_t aircraft_address{0};
    events::BluetoothAddress bravo{};
};

size_t sealed_v13(const V13& old, uint8_t* blob) {
    blob[0] = 13;
    std::memcpy(blob + 1, &old, sizeof(V13));
    const uint32_t crc = fec::crc32(blob, 1 + sizeof(V13));
    for (int i = 0; i < 4; i++) blob[1 + sizeof(V13) + i] = static_cast<uint8_t>(crc >> (8 * i));
    return 1 + sizeof(V13) + 4;
}

Status apply_json(SettingsStore& store, const char* json) {
    const events::RxFrame frame = config_frame(json);
    return store.apply(cbor::Reader(ConstByteSpan(frame.data.data(), frame.bytes)));
}

std::string default_fields(const SettingsStore& store) {
    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Defaults);
    store.write_default_fields(writer);
    const int bytes = writer.finish();
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

}  // namespace

// Both old offs silenced the buzzer and the haptic and kept the glass, which visual does.
TEST_CASE("settings: a version-13 blob keeps its pin, and both its offs come back as visual") {
    struct Case {
        bool enabled;
        uint8_t volume;
        Alarm alarm;
    };
    for (const Case each : {Case{false, 2, Alarm::Visual}, Case{true, 0, Alarm::Visual},
                            Case{true, 1, Alarm::Soft}, Case{true, 2, Alarm::Loud}}) {
        CAPTURE(int(each.volume));
        V13 old{};
        old.alarm_enabled = each.enabled;
        old.alarm_volume = each.volume;
        old.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
        std::memcpy(old.callsign, "F-JABC", 7);
        uint8_t blob[128] = {0};
        Settings out;
        REQUIRE(from_blob(blob, sealed_v13(old, blob), out) == Status::Ok);
        CHECK(out.alarm == each.alarm);
        CHECK(out.bravo == old.bravo);
        CHECK(std::string(out.callsign) == "F-JABC");
    }
}

TEST_CASE("settings: a Go that cannot sound ships on visual, reads loud as visual, refuses soft") {
    const ports::Capabilities silent = ports::Capability::Display;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, silent);

    store.restore_defaults();
    CHECK(settings.alarm == Alarm::Visual);
    CHECK(default_fields(store).find("\"alarm\":\"visual\"") != std::string::npos);

    uint8_t blob[128] = {0};
    const size_t bytes = sealed_v13(V13{}, blob);
    REQUIRE(store.adopt(blob, bytes) == Status::Ok);
    CHECK(settings.alarm == Alarm::Visual);

    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"soft\"}") == Status::OutOfRange);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"loud\"}") == Status::OutOfRange);
    CHECK(settings.alarm == Alarm::Visual);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"off\"}") == Status::Ok);
    CHECK(settings.alarm == Alarm::Off);
}

TEST_CASE("settings: a Go+ ships on loud and takes every step") {
    const ports::Capabilities sounding = ports::Capability::Buzzer | ports::Capability::Haptic;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, sounding);
    store.restore_defaults();
    CHECK(settings.alarm == Alarm::Loud);
    for (const char* step : {"off", "visual", "soft", "loud"}) {
        const std::string json = std::string("{\"command\":\"set\",\"alarm\":\"") + step + "\"}";
        CHECK(apply_json(store, json.c_str()) == Status::Ok);
    }
    CHECK(settings.alarm == Alarm::Loud);
}
