// Harness, not a test: a CBOR frame held to the key table schemas/link.v1.json gives its command.
#ifndef SKYBLIP_TEST_SUPPORT_REGISTRY_FRAMES_H
#define SKYBLIP_TEST_SUPPORT_REGISTRY_FRAMES_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/events/link.h"
#include "core/util/cbor.h"
#include "core/util/hex.h"
#include "simulator/world/link_names.h"

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

}  // namespace skyblip

#endif
