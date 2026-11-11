// Harness, not a test: a CBOR frame held to the key table schemas/link.v1.json gives its command.
#ifndef SKYBLIP_TEST_SUPPORT_REGISTRY_FRAMES_H
#define SKYBLIP_TEST_SUPPORT_REGISTRY_FRAMES_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/comms/link_registry.h"
#include "core/events/link.h"
#include "core/util/cbor.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "simulator/world/link_names.h"
#include "simulator/world/link_text.h"

namespace skyblip {

inline const simulator::link_names::Key* key_numbered(const simulator::link_names::Frame& frame,
                                                      uint32_t number) {
    for (int i = 0; i < frame.count; i++)
        if (frame.keys[i].number == number) return &frame.keys[i];
    return nullptr;
}

inline bool on_a_phone_endpoint(const simulator::link_names::Frame& frame) {
    return frame.endpoint != events::Endpoint::Peer;
}

inline bool names_code(const simulator::link_names::Enumeration& enumeration, uint64_t code) {
    for (int i = 0; i < enumeration.count; i++)
        if (enumeration.values[i].code == code) return true;
    return false;
}

inline bool carries_its_type(const simulator::link_names::Key& key, const cbor::Entry& entry) {
    switch (key.type) {
        case simulator::link_names::Type::Unsigned: return entry.type == cbor::Type::Unsigned;
        case simulator::link_names::Type::Integer:
            return entry.type == cbor::Type::Unsigned || entry.type == cbor::Type::Negative;
        case simulator::link_names::Type::Boolean: return entry.type == cbor::Type::Boolean;
        case simulator::link_names::Type::Text: return entry.type == cbor::Type::Text;
        case simulator::link_names::Type::Bytes:
            return entry.type == cbor::Type::Bytes &&
                   (key.bytes == 0 || entry.content.size() == static_cast<size_t>(key.bytes));
        case simulator::link_names::Type::UnsignedArray: return entry.type == cbor::Type::Array;
        case simulator::link_names::Type::Enum:
            return entry.type == cbor::Type::Unsigned &&
                   names_code(*key.enumeration, entry.argument);
    }
    return false;
}

inline std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '0');
    hex::write(bytes, out.data());
    return out;
}

inline const simulator::link_names::Frame* frame_coded(uint64_t code) {
    for (const simulator::link_names::Frame& frame : simulator::link_names::kFrames)
        if (frame.code == code) return &frame;
    return nullptr;
}

inline std::string missing_required_keys(const simulator::link_names::Frame& frame,
                                         const cbor::Reader& reader) {
    std::string missing;
    for (int i = 0; i < frame.count; i++) {
        if (!frame.keys[i].required) continue;
        bool present = false;
        for (int j = 0; j < reader.count() && !present; j++) {
            cbor::Entry entry;
            present = reader.entry(j, entry) && entry.key == frame.keys[i].number;
        }
        if (!present) missing += std::string(missing.empty() ? "" : " ") + frame.keys[i].name;
    }
    return missing;
}

inline std::string complete_registry_frame(const std::string& json) {
    uint8_t bytes[sizeof(events::RxFrame::data)];
    INFO(json);
    const int length =
        simulator::link_text::frame_from_text(json.c_str(), bytes, static_cast<int>(sizeof(bytes)));
    REQUIRE(length > 0);
    const cbor::Reader reader(ConstByteSpan(bytes, static_cast<size_t>(length)));
    REQUIRE(reader.valid());
    const simulator::link_names::Frame* frame =
        frame_coded(reader.read_unsigned(link::kCommandKey).value());
    REQUIRE(frame != nullptr);
    for (int i = 0; i < reader.count(); i++) {
        cbor::Entry entry;
        REQUIRE(reader.entry(i, entry));
        const simulator::link_names::Key* key = key_numbered(*frame, entry.key);
        REQUIRE(key != nullptr);
        REQUIRE_MESSAGE(carries_its_type(*key, entry), key->name);
    }
    REQUIRE(missing_required_keys(*frame, reader) == "");
    return std::string(reinterpret_cast<const char*>(bytes), static_cast<size_t>(length));
}

}  // namespace skyblip

#endif
