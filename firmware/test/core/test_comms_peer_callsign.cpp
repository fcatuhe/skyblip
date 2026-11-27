// The callsigns a Bravo hands a Go, held to the set the menu types, as a name off the air is.
#include <cstdint>
#include <string>

#include "core/comms/peer.h"
#include "doctest/doctest.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

ConstByteSpan span_of(const std::string& frame) {
    return ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

bool observation_named(const std::string& callsign) {
    const std::string frame = complete_registry_frame(
        "{\"command\":\"observation\",\"address\":4479337,\"boot_epoch\":9,\"sequence\":4,"
        "\"at_s\":1785628800,\"callsign\":\"" +
        callsign + "\"}");
    return comms::read_observation(span_of(frame)).ok();
}

bool transponder_named(const std::string& callsign) {
    const std::string frame = complete_registry_frame(
        "{\"command\":\"health\",\"protocol_version\":2,\"boot_epoch\":9,\"listening\":true,"
        "\"utc_valid\":true,\"aircraft_address_set\":false,\"export_reference\":\"own\","
        "\"paired\":true,\"charging\":false,\"power_level\":\"normal\","
        "\"own_transponder\":3820380,\"own_transponder_callsign\":\"" +
        callsign + "\"}");
    return comms::read_health(span_of(frame)).ok();
}

}  // namespace

// A Go copies the name into a $PFLAA ID field, where a comma or a dollar sign would forge fields.
TEST_CASE("peer frames: an observation whose callsign the menu cannot type is refused") {
    CHECK(observation_named("DLH-4 AB"));
    CHECK_FALSE(observation_named("A,0,0,0"));
    CHECK_FALSE(observation_named("*00$PFLA"));
    CHECK_FALSE(observation_named("dlh4ab"));
}

// The Go proposes the transponder's callsign as its own and draws it on the glass.
TEST_CASE("peer frames: a transponder's callsign the menu cannot type is refused") {
    CHECK(transponder_named("F-JA BC"));
    CHECK_FALSE(transponder_named("FJ,ABC"));
    CHECK_FALSE(transponder_named("$FJABC"));
    CHECK_FALSE(transponder_named("fjabc"));
}
