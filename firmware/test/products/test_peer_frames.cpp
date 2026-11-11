// The peer frames a paired Go and Bravo will speak, held to schemas/link.v1.json.
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/comms/link_registry.h"
#include "core/comms/peer.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "simulator/world/link_names.h"
#include "test/support/registry_frames.h"

using namespace skyblip;
namespace names = simulator::link_names;

namespace {

const std::map<std::string, int64_t> kNarrowerThanTheirType = {
    {"observation.address", 0xFFFFFF},
    {"observation.into_ms", 999},
    {"observation.latitude_1e7", -900000000},
    {"observation.longitude_1e7", -1800000000},
    {"observation.speed_q", std::numeric_limits<uint16_t>::max()},
    {"observation.track_c9", 511},
    {"observation.climb_e8", std::numeric_limits<int16_t>::min()},
    {"observation.aircraft_category", 17},
    {"observation.adsb_version", 7},
    {"observation.gva", 3},
    {"health.protocol_version", 1},
    {"peer_claim.into_ms", 999},
    {"peer_claim.latitude_1e7", -900000000},
    {"peer_claim.longitude_1e7", -1800000000},
    {"peer_claim.own_icao", 0xFFFFFF},
};

const std::map<std::string, std::string> kWidestText = {{"observation.callsign", "ABCD1234"}};

const std::map<std::string, std::string> kPeerFramesAtTheirWidest = {
    {"observation",
     "b81900181a011a00ffffff021affffffff031affffffff041affffffff051903e7063a35a4e8ff073a6b49d1"
     "ff083a7fffffff093a7fffffff0a3a7fffffff0b3a7fffffff0c19ffff0d1901ff0e3a7fffffff0f397fff10"
     "02113a7fffffff1202133a7fffffff1411150716031768414243443132333418181affffffff"},
    {"health",
     "ac00181b0101021affffffff03f504f5051affffffff06f507f5081affffffff091affffffff0a1affffffff"
     "0b02"},
    {"peer_claim",
     "b200181c011affffffff021affffffff031903e7043a35a4e8ff053a6b49d1ff061affffffff073a7fffffff"
     "081affffffff093a7fffffff0a1affffffff0b1a00ffffff0cf50d1affffffff0e1affffffff0f02101affff"
     "ffff111affffffff"},
};

int64_t widest_number(const std::string& name, int64_t type_extreme) {
    const auto narrower = kNarrowerThanTheirType.find(name);
    return narrower == kNarrowerThanTheirType.end() ? type_extreme : narrower->second;
}

std::vector<uint8_t> at_its_widest(const names::Frame& frame) {
    std::vector<uint8_t> bytes(256);
    cbor::Writer writer(bytes.data(), static_cast<int>(bytes.size()));
    writer.add_unsigned(link::kCommandKey, frame.code);
    for (int i = 0; i < frame.count; i++) {
        const names::Key& key = frame.keys[i];
        const std::string name = std::string(frame.command) + "." + key.name;
        switch (key.type) {
            case names::Type::Unsigned:
                writer.add_unsigned(key.number,
                                    widest_number(name, std::numeric_limits<uint32_t>::max()));
                break;
            case names::Type::Integer:
                writer.add_integer(key.number,
                                   widest_number(name, std::numeric_limits<int32_t>::min()));
                break;
            case names::Type::Boolean: writer.add_boolean(key.number, true); break;
            case names::Type::Text:
                writer.add_text(key.number, kWidestText.at(name).c_str());
                break;
            case names::Type::Enum:
                if (key.number != link::kCommandKey)
                    writer.add_unsigned(key.number,
                                        key.enumeration->values[key.enumeration->count - 1].code);
                break;
            case names::Type::Bytes:
            case names::Type::UnsignedArray: FAIL("no peer key is ", name); break;
        }
    }
    bytes.resize(static_cast<size_t>(writer.finish()));
    return bytes;
}

void carries_every_key_at_its_type(const names::Frame& frame, const std::vector<uint8_t>& bytes) {
    const cbor::Reader reader(ConstByteSpan(bytes.data(), bytes.size()));
    REQUIRE(reader.valid());
    CHECK(reader.count() == frame.count);
    for (int i = 0; i < reader.count(); i++) {
        cbor::Entry entry;
        REQUIRE(reader.entry(i, entry));
        const names::Key* key = key_numbered(frame, entry.key);
        REQUIRE(key != nullptr);
        CHECK_MESSAGE(carries_its_type(*key, entry), std::string(key->name));
    }
    if (frame.sender == names::Sender::Host)
        CHECK(reader.keys_within(link::host_keys(static_cast<link::Command>(frame.code))));
}

}  // namespace

// Bravo serves the peer frames in #192 and the Go folds observations in #193: these bytes are their
// contract.
TEST_CASE("link registry: the peer frames at their widest are these bytes, every key at its type") {
    std::set<std::string> pinned;
    for (const names::Frame& frame : names::kFrames) {
        if (on_a_phone_endpoint(frame)) continue;
        CAPTURE(std::string(frame.command));
        const std::vector<uint8_t> bytes = at_its_widest(frame);
        REQUIRE(kPeerFramesAtTheirWidest.count(frame.command) == 1);
        CHECK(hex_text(ConstByteSpan(bytes.data(), bytes.size())) ==
              kPeerFramesAtTheirWidest.at(frame.command));
        carries_every_key_at_its_type(frame, bytes);
        pinned.insert(frame.command);
    }
    CHECK(pinned.size() == kPeerFramesAtTheirWidest.size());
}

// core/comms/README.md writes this payload down, and a Go refuses a pairing below it (#177).
TEST_CASE("link registry: a pairing needs 126 bytes, the observation at its widest") {
    size_t widest = 0;
    std::string widest_command;
    for (const names::Frame& frame : names::kFrames) {
        if (on_a_phone_endpoint(frame)) continue;
        const size_t bytes = at_its_widest(frame).size();
        if (std::string(frame.command) == "peer_claim")
            CHECK(bytes == static_cast<size_t>(comms::kPeerClaimWidestBytes));
        if (bytes <= widest) continue;
        widest = bytes;
        widest_command = frame.command;
    }
    CHECK(widest == 126);
    CHECK(widest == comms::kPeerPayloadFloorBytes);
    CHECK(widest_command == "observation");
}
