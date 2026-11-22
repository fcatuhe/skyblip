// The one alarm setting, version 14: off, visual, soft and loud, and what a Go that cannot sound
// keeps.
#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

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

TEST_CASE("settings: a Go that cannot sound ships on visual, reads loud as visual, refuses soft" *
          doctest::test_suite("main")) {
    const ports::Capabilities silent = ports::Capability::Display;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, silent);

    store.restore_defaults();
    CHECK(settings.alarm == Alarm::Visual);
    CHECK(default_fields(store).find("\"alarm\":\"visual\"") != std::string::npos);

    Settings loud = defaults();
    loud.alarm = Alarm::Loud;
    uint8_t blob[128] = {0};
    to_blob(loud, blob, sizeof(blob));
    REQUIRE(store.adopt(blob, blob_size()) == Status::Ok);
    CHECK(settings.alarm == Alarm::Visual);

    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"soft\"}") == Status::OutOfRange);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"loud\"}") == Status::OutOfRange);
    CHECK(settings.alarm == Alarm::Visual);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"off\"}") == Status::Ok);
    CHECK(settings.alarm == Alarm::Off);
}

// A paired Go that cannot sound sets BRAVO SOFT and BRAVO LOUD, which only its Bravo sounds.
TEST_CASE("settings: a Go that cannot sound takes soft and loud only while paired with a Bravo" *
          doctest::test_suite("main")) {
    const ports::Capabilities silent = ports::Capability::Display;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, silent);
    store.restore_defaults();
    settings.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"soft\"}") == Status::Ok);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"loud\"}") == Status::Ok);
    CHECK(settings.alarm == Alarm::Loud);

    uint8_t blob[128] = {0};
    const size_t bytes = store.to_blob(blob, sizeof(blob));
    REQUIRE(bytes > 0);
    Settings read = defaults();
    SettingsStore reread(read, kTestActivation, kNoOwnTransponder, silent);
    REQUIRE(reread.adopt(blob, bytes) == Status::Ok);
    CHECK(read.alarm == Alarm::Loud);

    settings.bravo = events::BluetoothAddress{};
    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Config);
    store.write_fields(writer);
    const int written = writer.finish();
    CHECK(link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(written)))
              .find("\"alarm\":\"visual\"") != std::string::npos);
    CHECK(apply_json(store, "{\"command\":\"set\",\"alarm\":\"soft\"}") == Status::OutOfRange);
}

TEST_CASE("settings: a phone's soft on a paired Go that cannot sound is confirmed as BRAVO SOFT") {
    const ports::Capabilities silent = ports::Capability::Display;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, silent);
    store.restore_defaults();
    settings.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    const events::RxFrame frame = config_frame("{\"command\":\"set\",\"alarm\":\"soft\"}");
    char detail[common::kPromptDetailBytes]{};
    REQUIRE(store.describe_changes(cbor::Reader(ConstByteSpan(frame.data.data(), frame.bytes)),
                                   detail, sizeof(detail)) == Status::Ok);
    CHECK(std::string(detail) == "ALARM BRAVO SOFT");
}

namespace {

std::string config_fields(const SettingsStore& store) {
    uint8_t frame[64];
    cbor::Writer writer(frame, sizeof(frame));
    writer.add_code(link::kCommandKey, link::Command::Config);
    store.write_fields(writer);
    const int bytes = writer.finish();
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

}  // namespace

// A new pair sounds out of the box, as Bravo alone does, so a paired plain Go defaults to loud.
TEST_CASE(
    "settings: a Go that cannot sound defaults to BRAVO LOUD while paired, VISUAL otherwise") {
    const ports::Capabilities silent = ports::Capability::Display;
    Settings settings = defaults();
    SettingsStore store(settings, kTestActivation, kNoOwnTransponder, silent);
    store.restore_defaults();
    CHECK(default_fields(store).find("\"alarm\":\"visual\"") != std::string::npos);
    settings.bravo = events::BluetoothAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
    CHECK(default_fields(store).find("\"alarm\":\"loud\"") != std::string::npos);
}

// The briefing page reads this, so it never guesses the parts from a default.
TEST_CASE("settings: config says whether the Go can sound an alarm itself") {
    Settings plain_settings = defaults();
    const ports::Capabilities silent = ports::Capability::Display;
    const SettingsStore plain(plain_settings, kTestActivation, kNoOwnTransponder, silent);
    CHECK(config_fields(plain).find("\"annunciator_fitted\":false") != std::string::npos);
    CHECK(default_fields(plain).find("annunciator_fitted") == std::string::npos);

    Settings plus_settings = defaults();
    const ports::Capabilities sounding = ports::Capability::Buzzer | ports::Capability::Haptic;
    const SettingsStore plus(plus_settings, kTestActivation, kNoOwnTransponder, sounding);
    CHECK(config_fields(plus).find("\"annunciator_fitted\":true") != std::string::npos);
}

TEST_CASE("settings: a Go+ ships on loud and takes every step" * doctest::test_suite("main")) {
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
