// ADS-L F.2.3's "disallowing inconsistent user configurations", as the Go's settings refuse them.
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "core/flight/state.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "products/common/services/config_service.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"
#include "test/support/settings_link.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr uint32_t kHeard = 0x3C6586;
constexpr uint32_t kOther = 0x3C0A11;

// The set a page sends, answered as the page reads it, with the glass's press in between.
std::string answer_to_set(Settings& settings,
                          const std::optional<comms::OwnTransponderHeard>& own_transponder,
                          const char* json) {
    platform::host::Link link;
    link.raise_link(1);
    SettingsStore store(settings, kTestActivation, own_transponder);
    common::ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame(json));
    if (config.pending() == common::Pending::Set) config.confirm();
    return link_text(link.last().bytes);
}

}  // namespace

// ICAO Annex 10 Volume III, Appendix to Chapter 9, 5.2 f): 24 ones is assigned to no aircraft.
TEST_CASE("settings: an aircraft address of 24 ones is refused, and the one below it is stored") {
    Settings settings = defaults();
    CHECK(apply_patch(settings, "{\"aircraft_address\":16777215}") == Status::OutOfRange);
    CHECK(settings.aircraft_address == kAircraftAddressUnset);
    CHECK(apply_patch(settings, "{\"aircraft_address\":16777214}") == Status::Ok);
    CHECK(settings.aircraft_address == 0xFFFFFEu);

    settings.aircraft_address = 0xFFFFFF;
    CHECK(validate(settings) == Status::OutOfRange);
}

TEST_CASE(
    "settings: an aircraft address other than the transponder a paired Bravo names is refused" *
    doctest::test_suite("main")) {
    Settings settings = defaults();
    CHECK(apply_patch(settings, "{\"aircraft_address\":3934737,\"units\":\"metric\"}", kHeard) ==
          Status::Conflict);
    CHECK(settings.aircraft_address == kAircraftAddressUnset);
    CHECK(settings.units == Units::Nautical);

    CHECK(apply_patch(settings, "{\"aircraft_address\":3958150}", kHeard) == Status::Ok);
    CHECK(settings.aircraft_address == kHeard);
    CHECK(apply_patch(settings, "{\"aircraft_address\":3934737}") == Status::Ok);
    CHECK(settings.aircraft_address == kOther);
}

// F.2.3 asks for the setting to be revertible, and a set that leaves aircraft_address out did not
// enter it.
TEST_CASE(
    "settings: a clear, or a set without aircraft_address, is never refused for Bravo's "
    "transponder") {
    Settings settings = defaults();
    settings.aircraft_address = kOther;
    CHECK(apply_patch(settings, "{\"units\":\"metric\"}", kHeard) == Status::Ok);
    CHECK(settings.aircraft_address == kOther);
    CHECK(apply_patch(settings, "{\"aircraft_address\":0}", kHeard) == Status::Ok);
    CHECK(settings.aircraft_address == kAircraftAddressUnset);
}

TEST_CASE(
    "comms: a set of an aircraft address Bravo does not name is refused not_own_transponder") {
    Settings settings = defaults();
    const std::optional<comms::OwnTransponderHeard> heard = comms::OwnTransponderHeard{kHeard};
    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"aircraft_address\":3934737}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"not_own_"
          "transponder\"}");
    CHECK(settings.aircraft_address == kAircraftAddressUnset);

    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"aircraft_address\":3958150}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
    CHECK(settings.aircraft_address == kHeard);

    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"aircraft_address\":16777215}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"out_of_range\"}");
    CHECK(settings.aircraft_address == kHeard);
}

// Bravo can name another transponder between the prompt and the presses, so both are checked.
TEST_CASE(
    "comms: an aircraft address staged before Bravo named another is refused at the presses") {
    Settings settings = defaults();
    std::optional<comms::OwnTransponderHeard> heard{};
    platform::host::Link link;
    link.raise_link(1);
    SettingsStore store(settings, kTestActivation, heard);
    common::ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"command\":\"set\",\"aircraft_address\":3934737}"));
    REQUIRE(config.pending() == common::Pending::Set);

    heard = comms::OwnTransponderHeard{kHeard};
    config.confirm();
    CHECK(link_text(link.last().bytes) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"not_own_"
          "transponder\"}");
    CHECK(settings.aircraft_address == kAircraftAddressUnset);
}
