// Bravo's own blob, version 1, and the three keys of the link's table it stores (#176).
#include <cstring>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_bravo/settings.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

void confirm_set(BravoRig& rig, const char* json) {
    rig.send(json);
    rig.run_for(1000);
    rig.double_press();
    rig.run_for(1000);
}

}  // namespace

TEST_CASE("bravo: config answers alarm_enabled, alarm_volume and own_icao, and nothing else") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"get\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"config\",\"alarm_enabled\":true,\"alarm_volume\":\"loud\","
          "\"own_icao\":0}");
}

TEST_CASE("bravo: defaults name the same three keys, as a new unit ships them") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.product.settings().own_icao = 0x3C6586;
    rig.send("{\"command\":\"defaults\"}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"defaults\",\"alarm_enabled\":true,\"alarm_volume\":\"loud\","
          "\"own_icao\":0}");
}

TEST_CASE("bravo: a set stores Bravo's keys and ignores the Go's, as address is ignored") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    confirm_set(rig,
                "{\"command\":\"set\",\"aircraft_type\":4,\"units\":\"metric\","
                "\"callsign\":\"F-JABC\",\"alarm_volume\":\"soft\",\"own_icao\":3958150}");
    CHECK(rig.product.settings().alarm_volume == bravo::AlarmVolume::Soft);
    CHECK(rig.product.settings().own_icao == 3958150u);
    CHECK(rig.last_on(events::Endpoint::Config) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
}

TEST_CASE("bravo: a set past Bravo's range is refused at the door") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.send("{\"command\":\"set\",\"own_icao\":16777216}");
    rig.run_for(200);
    CHECK(rig.last_on(events::Endpoint::Config).find("out_of_range") != std::string::npos);
    CHECK(rig.product.button().prompt() == comms::Pending::None);
}

TEST_CASE("bravo: the settings come back from flash after a reboot") {
    platform::host::MeshtrackerX1 platform;
    {
        Bravo first{platform};
        REQUIRE(first.setup() == Status::Ok);
        first.settings().own_icao = 0x3C6586;
        first.settings().alarm_volume = bravo::AlarmVolume::Off;
        first.config_link().config().note_settings_changed();
        first.config_link().flush_settings(1000);
    }
    Bravo second{platform};
    REQUIRE(second.setup() == Status::Ok);
    CHECK(second.settings().own_icao == 0x3C6586u);
    CHECK(second.settings().alarm_volume == bravo::AlarmVolume::Off);
    CHECK_FALSE(second.settings().peer.pinned);
}

// #192 stores the pinned Go in version 1, so the field is in the blob from the first image.
TEST_CASE("bravo: blob version 1 carries the peer pin whole, and refuses any other version") {
    bravo::Settings pinned = bravo::defaults();
    pinned.peer.pinned = true;
    pinned.peer.address_type = 1;
    const uint8_t address[bravo::kPeerAddressBytes] = {0xC0, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(pinned.peer.address, address, sizeof(address));

    uint8_t blob[64]{};
    bravo::to_blob(pinned, blob, sizeof(blob));
    CHECK(settings::blob_version(blob) == 1);
    bravo::Settings read{};
    REQUIRE(bravo::from_blob(blob, bravo::blob_size(), read) == Status::Ok);
    CHECK(read.peer.pinned);
    CHECK(read.peer.address_type == 1);
    CHECK(std::memcmp(read.peer.address, address, sizeof(address)) == 0);

    blob[0] = 2;
    CHECK(bravo::from_blob(blob, bravo::blob_size(), read) == Status::Unsupported);
}
