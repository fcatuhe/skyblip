// ADS-L F.2.3's "disallowing inconsistent user configurations", as the Go's settings refuse them.
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "core/comms/config.h"
#include "core/flight/state.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/link.h"
#include "products/skyblip_go/settings.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/config_frame.h"
#include "test/support/settings_link.h"

using namespace skyblip;
using namespace skyblip::go;

namespace {

constexpr uint32_t kHeard = 0x3C6586;
constexpr uint32_t kOther = 0x3C0A11;

// The set a page sends, answered as the page reads it, with the glass's two presses in between.
std::string answer_to_set(Settings& settings, const std::optional<uint32_t>& own_transponder,
                          const char* json) {
    platform::host::Link link;
    link.raise_link(1);
    SettingsStore store(settings, kTestActivation, own_transponder);
    comms::ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame(json));
    if (config.pending() == comms::Pending::Set) config.confirm();
    return link_text(link.last().bytes);
}

}  // namespace

// ICAO Annex 10 Volume III, Appendix to Chapter 9, 5.2 f): 24 ones is assigned to no aircraft.
TEST_CASE("settings: an own ICAO of 24 ones is refused, and the one below it is stored") {
    Settings settings = defaults();
    CHECK(apply_patch(settings, "{\"own_icao\":16777215}") == Status::OutOfRange);
    CHECK(settings.own_icao == kOwnIcaoUnset);
    CHECK(apply_patch(settings, "{\"own_icao\":16777214}") == Status::Ok);
    CHECK(settings.own_icao == 0xFFFFFEu);

    settings.own_icao = 0xFFFFFF;
    CHECK(validate(settings) == Status::OutOfRange);
}

// An older build took 24 ones, so the blob that holds it loses that and keeps everything else.
TEST_CASE("settings: an own ICAO of 24 ones read from flash comes back unset, the rest kept") {
    Settings settings = defaults();
    std::strncpy(settings.callsign, "D-KXYZ", kCallsignBytes - 1);
    settings.own_icao = 0xFFFFFF;
    uint8_t blob[64];
    to_blob(settings, blob, sizeof(blob));

    Settings loaded;
    REQUIRE(from_blob(blob, blob_size(), loaded) == Status::Ok);
    CHECK(loaded.own_icao == kOwnIcaoUnset);
    CHECK(std::string(loaded.callsign) == "D-KXYZ");
}

TEST_CASE("settings: an own ICAO other than the transponder a paired Bravo names is refused") {
    Settings settings = defaults();
    CHECK(apply_patch(settings, "{\"own_icao\":3934737,\"units\":\"metric\"}", kHeard) ==
          Status::Conflict);
    CHECK(settings.own_icao == kOwnIcaoUnset);
    CHECK(settings.units == Units::Nautical);

    CHECK(apply_patch(settings, "{\"own_icao\":3958150}", kHeard) == Status::Ok);
    CHECK(settings.own_icao == kHeard);
    CHECK(apply_patch(settings, "{\"own_icao\":3934737}") == Status::Ok);
    CHECK(settings.own_icao == kOther);
}

// F.2.3 asks for the setting to be revertible, and a set that leaves own_icao out did not enter it.
TEST_CASE(
    "settings: a clear, or a set without own_icao, is never refused for Bravo's transponder") {
    Settings settings = defaults();
    settings.own_icao = kOther;
    CHECK(apply_patch(settings, "{\"units\":\"metric\"}", kHeard) == Status::Ok);
    CHECK(settings.own_icao == kOther);
    CHECK(apply_patch(settings, "{\"own_icao\":0}", kHeard) == Status::Ok);
    CHECK(settings.own_icao == kOwnIcaoUnset);
}

TEST_CASE("comms: a set of an own ICAO Bravo does not name is refused not_own_transponder") {
    Settings settings = defaults();
    const std::optional<uint32_t> heard = kHeard;
    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"own_icao\":3934737}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"not_own_"
          "transponder\"}");
    CHECK(settings.own_icao == kOwnIcaoUnset);

    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"own_icao\":3958150}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":true}");
    CHECK(settings.own_icao == kHeard);

    CHECK(answer_to_set(settings, heard, "{\"command\":\"set\",\"own_icao\":16777215}") ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"out_of_range\"}");
    CHECK(settings.own_icao == kHeard);
}

// Bravo can name another transponder between the prompt and the presses, so both are checked.
TEST_CASE("comms: an own ICAO staged before Bravo named another is refused at the presses") {
    Settings settings = defaults();
    std::optional<uint32_t> heard{};
    platform::host::Link link;
    link.raise_link(1);
    SettingsStore store(settings, kTestActivation, heard);
    comms::ConfigService config(link, store);
    config.set_flight_state(flight::FlightState::Ground);
    config.on_rx(config_frame("{\"command\":\"set\",\"own_icao\":3934737}"));
    REQUIRE(config.pending() == comms::Pending::Set);

    heard = kHeard;
    config.confirm();
    CHECK(link_text(link.last().bytes) ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"not_own_"
          "transponder\"}");
    CHECK(settings.own_icao == kOwnIcaoUnset);
}
