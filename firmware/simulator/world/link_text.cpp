#include "simulator/world/link_text.h"

#include <cstring>

#include "core/comms/link_registry.h"
#include "core/util/cbor.h"
#include "core/util/hex.h"
#include "simulator/world/json.h"
#include "simulator/world/link_names.h"

namespace skyblip::simulator::link_text {

namespace {

using link_names::Enumeration;
using link_names::Frame;
using link_names::Key;
using link_names::Type;
using link_names::Value;

constexpr int kTextBytes = 2 * sizeof(events::RxFrame::data) + 2;

const Frame* frame_named(const char* command) {
    for (const Frame& frame : link_names::kFrames)
        if (std::strcmp(frame.command, command) == 0) return &frame;
    return nullptr;
}

const Frame* frame_coded(uint64_t code) {
    for (const Frame& frame : link_names::kFrames)
        if (frame.code == code) return &frame;
    return nullptr;
}

const Key* key_numbered(const Frame& frame, uint32_t number) {
    for (int i = 0; i < frame.count; i++)
        if (frame.keys[i].number == number) return &frame.keys[i];
    return nullptr;
}

const Value* value_named(const Enumeration& enumeration, const char* name) {
    for (int i = 0; i < enumeration.count; i++)
        if (std::strcmp(enumeration.values[i].name, name) == 0) return &enumeration.values[i];
    return nullptr;
}

const Value* value_coded(const Enumeration& enumeration, uint64_t code) {
    for (int i = 0; i < enumeration.count; i++)
        if (enumeration.values[i].code == code) return &enumeration.values[i];
    return nullptr;
}

bool add_text_value(cbor::Writer& writer, const Key& key, const char* text) {
    if (key.type == Type::Enum) {
        const Value* value = value_named(*key.enumeration, text);
        if (value != nullptr) {
            writer.add_unsigned(key.number, value->code);
            return true;
        }
    }
    if (key.type == Type::Bytes) {
        const size_t digits = std::strlen(text);
        uint8_t bytes[sizeof(events::RxFrame::data)];
        if (digits % 2 == 0 && digits / 2 <= sizeof(bytes) &&
            hex::read(text, ByteSpan(bytes, digits / 2))) {
            writer.add_bytes(key.number, ConstByteSpan(bytes, digits / 2));
            return true;
        }
    }
    writer.add_text(key.number, text);
    return true;
}

bool add_key(cbor::Writer& writer, const json::Reader& reader, const Key& key) {
    if (!reader.has(key.name)) return true;
    long number = 0;
    uint32_t large = 0;
    bool flag = false;
    char text[kTextBytes];
    if (reader.read_integer(key.name, number)) {
        writer.add_integer(key.number, number);
    } else if (reader.read_unsigned(key.name, large)) {
        writer.add_unsigned(key.number, large);
    } else if (reader.read_boolean(key.name, flag)) {
        writer.add_boolean(key.number, flag);
    } else if (reader.read_string(key.name, text, sizeof(text))) {
        return add_text_value(writer, key, text);
    } else {
        return false;
    }
    return true;
}

void append_quoted(std::string& out, const char* text, size_t bytes) {
    out += '"';
    for (size_t i = 0; i < bytes; i++) {
        if (text[i] == '"' || text[i] == '\\') out += '\\';
        out += text[i];
    }
    out += '"';
}

void append_value(std::string& out, const cbor::Entry& entry, const Key* key) {
    switch (entry.type) {
        case cbor::Type::Unsigned: {
            const Value* value = key != nullptr && key->type == Type::Enum
                                     ? value_coded(*key->enumeration, entry.argument)
                                     : nullptr;
            if (value != nullptr)
                append_quoted(out, value->name, std::strlen(value->name));
            else
                out += std::to_string(entry.argument);
            return;
        }
        case cbor::Type::Negative:
            out += '-';
            out += entry.argument == UINT64_MAX ? std::string("18446744073709551616")
                                                : std::to_string(entry.argument + 1);
            return;
        case cbor::Type::Boolean: out += entry.argument != 0 ? "true" : "false"; return;
        case cbor::Type::Text:
            append_quoted(out, reinterpret_cast<const char*>(entry.content.data()),
                          entry.content.size());
            return;
        case cbor::Type::Bytes: {
            std::string digits(2 * entry.content.size(), '0');
            hex::write(entry.content, digits.data());
            append_quoted(out, digits.data(), digits.size());
            return;
        }
        case cbor::Type::Array:
            out += '[';
            for (uint64_t i = 0; i < entry.argument; i++) {
                if (i > 0) out += ',';
                out += std::to_string(cbor::Reader::array_item(entry, static_cast<int>(i)).value());
            }
            out += ']';
            return;
    }
}

}  // namespace

int frame_from_text(const char* json, uint8_t* out, int capacity) {
    const json::Reader reader(json, static_cast<int>(std::strlen(json)));
    cbor::Writer writer(out, capacity);
    const Frame* frame = nullptr;
    char command[kTextBytes];
    long code = 0;
    int keys_read = 0;
    if (reader.read_string("command", command, sizeof(command))) {
        frame = frame_named(command);
        if (frame == nullptr) return 0;
        writer.add_unsigned(link::kCommandKey, frame->code);
        keys_read++;
    } else if (reader.read_integer("command", code)) {
        writer.add_integer(link::kCommandKey, code);
        if (code >= 0) frame = frame_coded(static_cast<uint64_t>(code));
        keys_read++;
    }
    if (frame != nullptr) {
        for (int i = 0; i < frame->count; i++) {
            const Key& key = frame->keys[i];
            if (key.number == link::kCommandKey || !reader.has(key.name)) continue;
            if (!add_key(writer, reader, key)) return 0;
            keys_read++;
        }
    }
    if (keys_read != reader.key_count()) return 0;
    return writer.finish();
}

std::string text_from_frame(ConstByteSpan frame) {
    const cbor::Reader reader(frame);
    if (!reader.valid()) return std::string();
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    const Frame* table = code.ok() ? frame_coded(code.value()) : nullptr;
    std::string out = "{";
    for (int i = 0; i < reader.count(); i++) {
        cbor::Entry entry;
        reader.entry(i, entry);
        const Key* key = table != nullptr ? key_numbered(*table, entry.key) : nullptr;
        if (i > 0) out += ',';
        const std::string name = key != nullptr ? key->name : std::to_string(entry.key);
        append_quoted(out, name.data(), name.size());
        out += ':';
        append_value(out, entry, key);
    }
    out += '}';
    return out;
}

}  // namespace skyblip::simulator::link_text
