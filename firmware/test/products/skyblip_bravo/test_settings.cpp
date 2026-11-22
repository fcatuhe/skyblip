// Bravo's own blob, its versions, and the keys of the link's table it stores (#176).
#include <cstring>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_bravo/capture_settings.h"
#include "products/skyblip_bravo/settings.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

void confirm_set(BravoRig& rig, const char* json) {
    rig.send(json);
    rig.run_for(1000);
    rig.press();
    rig.run_for(1000);
}

}  // namespace

TEST_CASE("bravo: config answers aircraft_address, alarm and alarm_from_go, and nothing else") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"get\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"config\",\"aircraft_address\":0,\"alarm\":\"loud\","
          "\"alarm_from_go\":false}");
}

// The briefing shows a paired Bravo's alarm as its Go's, which the Go's ALARM row sets.
TEST_CASE("bravo: paired, config answers its Go's alarm, and a set of alarm is refused set_on_go") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.product.settings().alarm = bravo::Alarm::Visual;
    rig.product.settings().go_alarm = bravo::Alarm::Soft;
    rig.product.settings().peer = bravo::pin_of(events::BluetoothAddress{1, {1, 2, 3, 4, 5, 0xC6}});
    rig.send("{\"command\":\"get\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"config\",\"aircraft_address\":0,\"alarm\":\"soft\","
          "\"alarm_from_go\":true}");

    rig.send("{\"command\":\"set\",\"alarm\":\"soft\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"set_on_go\"}");
    CHECK(rig.product.button().prompt() == common::Pending::None);
    CHECK(rig.product.settings().alarm == bravo::Alarm::Visual);

    confirm_set(rig, "{\"command\":\"set\",\"aircraft_address\":3958150,\"alarm_from_go\":false}");
    CHECK(rig.product.settings().aircraft_address == 3958150u);
    CHECK(rig.product.settings().peer.pinned);
}

TEST_CASE("bravo: defaults name the same two keys, as a new unit ships them") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.product.settings().aircraft_address = 0x3C6586;
    rig.send("{\"command\":\"defaults\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"defaults\",\"aircraft_address\":0,\"alarm\":\"loud\"}");
}

TEST_CASE("bravo: a set stores Bravo's keys and ignores the Go's, as address is ignored") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    confirm_set(rig,
                "{\"command\":\"set\",\"aircraft_type\":4,\"units\":\"metric\","
                "\"callsign\":\"F-JABC\",\"alarm\":\"soft\",\"aircraft_address\":3958150}");
    CHECK(rig.product.settings().alarm == bravo::Alarm::Soft);
    CHECK(rig.product.settings().aircraft_address == 3958150u);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
}

TEST_CASE("bravo: a set past Bravo's range is refused at the door") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"set\",\"aircraft_address\":16777216}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config).find("out_of_range") != std::string::npos);
    CHECK(rig.product.button().prompt() == common::Pending::None);
}

TEST_CASE("bravo: the settings come back from flash after a reboot") {
    platform::host::MeshtrackerX1 platform;
    {
        Bravo first{platform};
        REQUIRE(first.setup() == Status::Ok);
        first.settings().aircraft_address = 0x3C6586;
        first.settings().alarm = bravo::Alarm::Off;
        first.config_link().config().note_settings_changed();
        first.config_link().flush_settings(1000);
    }
    Bravo second{platform};
    REQUIRE(second.setup() == Status::Ok);
    CHECK(second.settings().aircraft_address == 0x3C6586u);
    CHECK(second.settings().alarm == bravo::Alarm::Off);
    CHECK_FALSE(second.settings().peer.pinned);
}

TEST_CASE("bravo: a capture records the alarm Bravo plays, its Go's while paired") {
    bravo::Settings settings = bravo::defaults();
    settings.alarm = bravo::Alarm::Visual;
    settings.go_alarm = bravo::Alarm::Soft;
    const bravo::CaptureSettings capture(settings);
    diagnostics::Config record{};
    capture.describe(record);
    CHECK(record.alarm_setting == static_cast<uint8_t>(bravo::Alarm::Visual));
    settings.peer.pinned = true;
    capture.describe(record);
    CHECK(record.alarm_setting == static_cast<uint8_t>(bravo::Alarm::Soft));
    CHECK(record.settings_version == 4);
}

// #192 stores the pinned Go from the first image, and version 4 keeps it with its Go's alarm.
TEST_CASE("bravo: blob version 4 carries the peer pin and the Go's alarm, and no other version") {
    bravo::Settings pinned = bravo::defaults();
    pinned.peer.pinned = true;
    pinned.peer.address_type = 1;
    const uint8_t address[bravo::kPeerAddressBytes] = {0xC0, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(pinned.peer.address, address, sizeof(address));
    pinned.go_alarm = bravo::Alarm::Visual;

    uint8_t blob[64]{};
    bravo::to_blob(pinned, blob, sizeof(blob));
    CHECK(settings::blob_version(blob) == 4);
    bravo::Settings read{};
    REQUIRE(bravo::from_blob(blob, bravo::blob_size(), read) == Status::Ok);
    CHECK(read.peer.pinned);
    CHECK(read.peer.address_type == 1);
    CHECK(std::memcmp(read.peer.address, address, sizeof(address)) == 0);
    CHECK(read.go_alarm == bravo::Alarm::Visual);

    blob[0] = 5;
    CHECK(bravo::from_blob(blob, bravo::blob_size(), read) == Status::Unsupported);
    settings::seal(3, &pinned, sizeof(pinned), blob, sizeof(blob));
    CHECK(bravo::from_blob(blob, bravo::blob_size(), read) == Status::Unsupported);
}
