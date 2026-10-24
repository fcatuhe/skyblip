#include "core/util/cbor.h"

#include <cstring>

namespace skyblip::cbor {

namespace {

constexpr uint8_t kAdditionalMask = 0x1F;
constexpr uint8_t kOneByteArgument = 24;
constexpr uint8_t kFourByteArgument = 26;
constexpr uint8_t kFalseValue = 20;
constexpr uint8_t kTrueValue = 21;

struct Head {
    Major major{Major::Unsigned};
    uint8_t additional{0};
    uint64_t argument{0};
};

bool read_head(ConstByteSpan frame, size_t& at, Head& out) {
    if (at >= frame.size()) return false;
    const uint8_t initial = frame[at++];
    out.major = static_cast<Major>(initial >> 5);
    out.additional = initial & kAdditionalMask;
    if (out.additional < kOneByteArgument) {
        out.argument = out.additional;
        return true;
    }
    if (out.additional > kFourByteArgument) return false;
    const size_t width = size_t{1} << (out.additional - kOneByteArgument);
    if (frame.size() - at < width) return false;
    uint64_t argument = 0;
    for (size_t i = 0; i < width; i++) argument = (argument << 8) | frame[at++];
    out.argument = argument;
    return head_bytes(argument) == static_cast<int>(1 + width);
}

bool valid_utf8(ConstByteSpan text) {
    size_t at = 0;
    while (at < text.size()) {
        const uint8_t lead = text[at];
        size_t continuations = 0;
        uint32_t code = 0;
        uint32_t floor = 0;
        if (lead < 0x80) {
            at++;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            continuations = 1;
            code = lead & 0x1Fu;
            floor = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            continuations = 2;
            code = lead & 0x0Fu;
            floor = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            continuations = 3;
            code = lead & 0x07u;
            floor = 0x10000;
        } else {
            return false;
        }
        if (text.size() - at - 1 < continuations) return false;
        for (size_t i = 1; i <= continuations; i++) {
            const uint8_t next = text[at + i];
            if ((next & 0xC0) != 0x80) return false;
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < floor || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
        at += continuations + 1;
    }
    return true;
}

bool read_value(ConstByteSpan frame, size_t& at, Entry& out) {
    Head head;
    if (!read_head(frame, at, head)) return false;
    out.argument = head.argument;
    out.content = ConstByteSpan();
    switch (head.major) {
        case Major::Unsigned: out.type = Type::Unsigned; return true;
        case Major::Negative: out.type = Type::Negative; return true;
        case Major::Bytes:
        case Major::Text:
            if (head.argument > frame.size() - at) return false;
            out.type = head.major == Major::Bytes ? Type::Bytes : Type::Text;
            out.content = frame.subspan(at, static_cast<size_t>(head.argument));
            at += static_cast<size_t>(head.argument);
            return out.type == Type::Bytes || valid_utf8(out.content);
        case Major::Array: {
            if (head.argument > frame.size() - at) return false;
            const size_t items_start = at;
            for (uint64_t i = 0; i < head.argument; i++) {
                Head item;
                if (!read_head(frame, at, item) || item.major != Major::Unsigned) return false;
            }
            out.type = Type::Array;
            out.content = frame.subspan(items_start, at - items_start);
            return true;
        }
        case Major::Simple:
            if (head.additional != kFalseValue && head.additional != kTrueValue) return false;
            out.type = Type::Boolean;
            out.argument = head.additional == kTrueValue ? 1 : 0;
            return true;
        case Major::Map:
        case Major::Tag: break;
    }
    return false;
}

bool read_entry(ConstByteSpan frame, size_t& at, Entry& out) {
    Head key;
    if (!read_head(frame, at, key) || key.major != Major::Unsigned || key.argument > UINT32_MAX)
        return false;
    out.key = static_cast<uint32_t>(key.argument);
    return read_value(frame, at, out);
}

}  // namespace

int head_bytes(uint64_t argument) {
    if (argument < kOneByteArgument) return 1;
    if (argument <= UINT8_MAX) return 2;
    if (argument <= UINT16_MAX) return 3;
    if (argument <= UINT32_MAX) return 5;
    return 9;
}

int text_bytes(const char* text) { return static_cast<int>(std::strlen(text)); }

int unsigned_entry_bytes(uint32_t key, uint64_t value) {
    return head_bytes(key) + head_bytes(value);
}

int integer_entry_bytes(uint32_t key, int64_t value) {
    const uint64_t argument =
        value < 0 ? static_cast<uint64_t>(-(value + 1)) : static_cast<uint64_t>(value);
    return head_bytes(key) + head_bytes(argument);
}

int boolean_entry_bytes(uint32_t key) { return head_bytes(key) + 1; }

int text_entry_bytes(uint32_t key, const char* text) {
    const int bytes = text_bytes(text);
    return head_bytes(key) + head_bytes(static_cast<uint64_t>(bytes)) + bytes;
}

int bytes_entry_bytes(uint32_t key, size_t bytes) {
    return head_bytes(key) + head_bytes(bytes) + static_cast<int>(bytes);
}

int unsigned_array_entry_bytes(uint32_t key, const uint32_t* values, int count) {
    int bytes = head_bytes(key) + head_bytes(static_cast<uint64_t>(count));
    for (int i = 0; i < count; i++) bytes += head_bytes(values[i]);
    return bytes;
}

bool MapBudget::take(int entry_bytes) {
    const int head = entries_ + 1 < kOneByteArgument ? 1 : 2;
    if (used_ + entry_bytes + head > payload_bytes_) return false;
    used_ += entry_bytes;
    entries_++;
    return true;
}

Writer::Writer(uint8_t* out, int capacity) : out_(out), capacity_(capacity) {
    if (capacity_ < 1) refused_ = true;
}

bool Writer::open_entry(uint32_t key, int entry_bytes) {
    if (refused_) return false;
    const bool head_grows = entries_ + 1 == kOneByteArgument;
    const int needed = entry_bytes + (head_grows ? 1 : 0);
    if (static_cast<int64_t>(key) <= last_key_ || entries_ == kMaxEntries ||
        needed > capacity_ - length_) {
        refused_ = true;
        return false;
    }
    if (head_grows) {
        std::memmove(out_ + 2, out_ + 1, static_cast<size_t>(length_ - 1));
        length_++;
    }
    last_key_ = key;
    entries_++;
    write_head(Major::Unsigned, key);
    return true;
}

void Writer::write_head(Major major, uint64_t argument) {
    const uint8_t type = static_cast<uint8_t>(static_cast<uint8_t>(major) << 5);
    const int bytes = head_bytes(argument);
    if (bytes == 1) {
        out_[length_++] = static_cast<uint8_t>(type | argument);
        return;
    }
    const int width = bytes - 1;
    const uint8_t additional = width == 1   ? kOneByteArgument
                               : width == 2 ? kOneByteArgument + 1
                                            : kFourByteArgument;
    out_[length_++] = static_cast<uint8_t>(type | additional);
    for (int shift = (width - 1) * 8; shift >= 0; shift -= 8)
        out_[length_++] = static_cast<uint8_t>(argument >> shift);
}

void Writer::add_unsigned(uint32_t key, uint64_t value) {
    if (value > UINT32_MAX) refused_ = true;
    if (!open_entry(key, unsigned_entry_bytes(key, value))) return;
    write_head(Major::Unsigned, value);
}

void Writer::add_integer(uint32_t key, int64_t value) {
    if (value < -int64_t{UINT32_MAX} - 1 || value > int64_t{UINT32_MAX}) refused_ = true;
    if (!open_entry(key, integer_entry_bytes(key, value))) return;
    if (value < 0)
        write_head(Major::Negative, static_cast<uint64_t>(-(value + 1)));
    else
        write_head(Major::Unsigned, static_cast<uint64_t>(value));
}

void Writer::add_boolean(uint32_t key, bool value) {
    if (!open_entry(key, boolean_entry_bytes(key))) return;
    out_[length_++] = value ? kTrue : kFalse;
}

void Writer::add_text(uint32_t key, const char* text) {
    const size_t bytes = std::strlen(text);
    if (!valid_utf8(ConstByteSpan(reinterpret_cast<const uint8_t*>(text), bytes))) refused_ = true;
    if (!open_entry(key, text_entry_bytes(key, text))) return;
    write_head(Major::Text, bytes);
    std::memcpy(out_ + length_, text, bytes);
    length_ += static_cast<int>(bytes);
}

void Writer::add_bytes(uint32_t key, ConstByteSpan bytes) {
    if (!open_entry(key, bytes_entry_bytes(key, bytes.size()))) return;
    write_head(Major::Bytes, bytes.size());
    if (!bytes.empty()) std::memcpy(out_ + length_, bytes.data(), bytes.size());
    length_ += static_cast<int>(bytes.size());
}

void Writer::add_unsigned_array(uint32_t key, const uint32_t* values, int count) {
    if (!open_entry(key, unsigned_array_entry_bytes(key, values, count))) return;
    write_head(Major::Array, static_cast<uint64_t>(count));
    for (int i = 0; i < count; i++) write_head(Major::Unsigned, values[i]);
}

int Writer::finish() {
    if (refused_) return 0;
    const uint8_t map = static_cast<uint8_t>(static_cast<uint8_t>(Major::Map) << 5);
    if (entries_ < kOneByteArgument) {
        out_[0] = static_cast<uint8_t>(map | entries_);
    } else {
        out_[0] = static_cast<uint8_t>(map | kOneByteArgument);
        out_[1] = static_cast<uint8_t>(entries_);
    }
    return length_;
}

Reader::Reader(ConstByteSpan frame) : frame_(frame) {
    size_t at = 0;
    Head map;
    if (!read_head(frame_, at, map) || map.major != Major::Map || map.argument > frame_.size())
        return;
    first_entry_ = at;
    int64_t last_key = -1;
    for (uint64_t i = 0; i < map.argument; i++) {
        Entry entry;
        if (!read_entry(frame_, at, entry) || static_cast<int64_t>(entry.key) <= last_key) return;
        last_key = entry.key;
    }
    if (at != frame_.size()) return;
    count_ = static_cast<int>(map.argument);
    valid_ = true;
}

bool Reader::entry(int index, Entry& out) const {
    if (index < 0 || index >= count()) return false;
    size_t at = first_entry_;
    for (int i = 0; i <= index; i++) read_entry(frame_, at, out);
    return true;
}

bool Reader::keys_within(uint64_t key_bits) const {
    size_t at = first_entry_;
    for (int i = 0; i < count(); i++) {
        Entry entry;
        read_entry(frame_, at, entry);
        if (entry.key >= 64 || ((key_bits >> entry.key) & 1) == 0) return false;
    }
    return true;
}

bool Reader::find(uint32_t key, Entry& out) const {
    size_t at = first_entry_;
    for (int i = 0; i < count(); i++) {
        read_entry(frame_, at, out);
        if (out.key == key) return true;
        if (out.key > key) return false;
    }
    return false;
}

Result<uint32_t> Reader::read_unsigned(uint32_t key) const {
    Entry entry;
    if (!find(key, entry)) return Status::NotFound;
    if (entry.type != Type::Unsigned) return Status::Invalid;
    if (entry.argument > UINT32_MAX) return Status::OutOfRange;
    return static_cast<uint32_t>(entry.argument);
}

Result<int32_t> Reader::read_integer(uint32_t key) const {
    Entry entry;
    if (!find(key, entry)) return Status::NotFound;
    if (entry.type != Type::Unsigned && entry.type != Type::Negative) return Status::Invalid;
    if (entry.argument > INT32_MAX) return Status::OutOfRange;
    const int32_t magnitude = static_cast<int32_t>(entry.argument);
    return entry.type == Type::Unsigned ? magnitude : -1 - magnitude;
}

Result<bool> Reader::read_boolean(uint32_t key) const {
    Entry entry;
    if (!find(key, entry)) return Status::NotFound;
    if (entry.type != Type::Boolean) return Status::Invalid;
    return entry.argument != 0;
}

Status Reader::read_text(uint32_t key, char* out, int capacity) const {
    Entry entry;
    if (!find(key, entry)) return Status::NotFound;
    if (entry.type != Type::Text) return Status::Invalid;
    if (entry.content.size() + 1 > static_cast<size_t>(capacity)) return Status::OutOfRange;
    for (uint8_t character : entry.content)
        if (character == 0) return Status::Invalid;
    if (!entry.content.empty()) std::memcpy(out, entry.content.data(), entry.content.size());
    out[entry.content.size()] = 0;
    return Status::Ok;
}

Status Reader::read_bytes(uint32_t key, ByteSpan out) const {
    Entry entry;
    if (!find(key, entry)) return Status::NotFound;
    if (entry.type != Type::Bytes) return Status::Invalid;
    if (entry.content.size() != out.size()) return Status::OutOfRange;
    if (!out.empty()) std::memcpy(out.data(), entry.content.data(), out.size());
    return Status::Ok;
}

Result<uint32_t> Reader::array_item(const Entry& array, int index) {
    if (array.type != Type::Array || index < 0 || static_cast<uint64_t>(index) >= array.argument)
        return Status::NotFound;
    size_t at = 0;
    Head item;
    for (int i = 0; i <= index; i++) read_head(array.content, at, item);
    if (item.argument > UINT32_MAX) return Status::OutOfRange;
    return static_cast<uint32_t>(item.argument);
}

}  // namespace skyblip::cbor
