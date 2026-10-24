// Every frame the registry lists, named in JSON, through the simulator's codec and back.
#include <cstdint>
#include <string>

#include "core/comms/link_registry.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "simulator/world/link_names.h"
#include "simulator/world/link_text.h"

using namespace skyblip;
using namespace skyblip::simulator;

namespace {

std::string sample(const link_names::Key& key) {
    switch (key.type) {
        case link_names::Type::Unsigned: return "70000";
        case link_names::Type::Integer: return "-300";
        case link_names::Type::Boolean: return "true";
        case link_names::Type::Text: return "\"D-KXYZ\"";
        case link_names::Type::Bytes:
            return "\"" + std::string(2 * (key.bytes > 0 ? key.bytes : 30), 'a') + "\"";
        case link_names::Type::Enum:
            return std::string("\"") + key.enumeration->values[key.enumeration->count - 1].name +
                   "\"";
        case link_names::Type::UnsignedArray: break;
    }
    return "";
}

}  // namespace

// The registry generates the device's numbers and these names: a frame that fails is a bad table.
TEST_CASE("link text: every frame and every key the registry lists survives the round trip") {
    for (const link_names::Frame& frame : link_names::kFrames) {
        std::string json = std::string("{\"command\":\"") + frame.command + "\"";
        for (int i = 0; i < frame.count; i++) {
            const link_names::Key& key = frame.keys[i];
            if (key.number == link::kCommandKey || key.type == link_names::Type::UnsignedArray)
                continue;
            json += std::string(",\"") + key.name + "\":" + sample(key);
        }
        json += "}";
        CAPTURE(json);

        uint8_t encoded[512];
        const int bytes =
            link_text::frame_from_text(json.c_str(), encoded, static_cast<int>(sizeof(encoded)));
        REQUIRE(bytes > 0);
        const ConstByteSpan written(encoded, static_cast<size_t>(bytes));
        const cbor::Reader reader(written);
        REQUIRE(reader.valid());
        CHECK(reader.read_unsigned(link::kCommandKey).value() == frame.code);
        CHECK(link_text::text_from_frame(written) == json);
    }
}

TEST_CASE("link text: a name the registry does not hold is refused, a number is passed on") {
    uint8_t out[32];
    CHECK(link_text::frame_from_text("{\"command\":\"apply\"}", out, sizeof(out)) == 0);
    const int bytes = link_text::frame_from_text("{\"command\":99}", out, sizeof(out));
    REQUIRE(bytes > 0);
    CHECK(link_text::text_from_frame(ConstByteSpan(out, static_cast<size_t>(bytes))) ==
          "{\"0\":99}");
}

TEST_CASE("link text: the timing histograms read back as arrays") {
    uint8_t out[32];
    cbor::Writer writer(out, sizeof(out));
    const uint32_t buckets[] = {0, 24, 70000};
    writer.add_code(link::kCommandKey, link::Command::Timing);
    writer.add_unsigned_array(link::timing_key::kPpsUs, buckets, 3);
    const int bytes = writer.finish();
    CHECK(link_text::text_from_frame(ConstByteSpan(out, static_cast<size_t>(bytes))) ==
          "{\"command\":\"timing\",\"pps_us\":[0,24,70000]}");
}
