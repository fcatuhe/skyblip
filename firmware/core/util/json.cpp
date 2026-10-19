#include "core/util/json.h"

#include <cstring>

namespace skyblip::json {

namespace {
bool is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}

int Reader::value_offset(const char* key) const {
    int key_length = static_cast<int>(std::strlen(key));
    int i = 0;
    while (i < bytes_) {
        if (data_[i] != '"') {
            i++;
            continue;
        }
        int key_start = i + 1;
        int key_end = key_start;
        while (key_end < bytes_ && data_[key_end] != '"') key_end++;
        if (key_end >= bytes_) break;
        bool match = (key_end - key_start == key_length);
        for (int j = 0; match && j < key_length; j++)
            if (data_[key_start + j] != key[j]) match = false;
        i = key_end + 1;
        while (i < bytes_ && is_whitespace(data_[i])) i++;
        if (i < bytes_ && data_[i] == ':') {
            i++;
            while (i < bytes_ && is_whitespace(data_[i])) i++;
            if (match) return i < bytes_ ? i : -1;
        }
    }
    return -1;
}

bool Reader::has(const char* key) const { return value_offset(key) >= 0; }

bool Reader::read_integer(const char* key, long& out) const {
    int64_t value = 0;
    if (!read_number(key, value) || value < INT32_MIN || value > INT32_MAX) return false;
    out = static_cast<long>(value);
    return true;
}

bool Reader::read_unsigned(const char* key, uint32_t& out) const {
    int64_t value = 0;
    if (!read_number(key, value) || value < 0 || value > UINT32_MAX) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

bool Reader::read_number(const char* key, int64_t& out) const {
    int offset = value_offset(key);
    if (offset < 0) return false;
    bool negative = false;
    if (data_[offset] == '-') {
        negative = true;
        offset++;
    }
    if (offset >= bytes_ || data_[offset] < '0' || data_[offset] > '9') return false;
    int64_t value = 0;
    while (offset < bytes_ && data_[offset] >= '0' && data_[offset] <= '9') {
        value = value * 10 + (data_[offset++] - '0');
        if (value > UINT32_MAX) return false;
    }
    out = negative ? -value : value;
    return true;
}

bool Reader::read_boolean(const char* key, bool& out) const {
    int offset = value_offset(key);
    if (offset < 0) return false;
    if (offset + 4 <= bytes_ && data_[offset] == 't') {
        out = true;
        return true;
    }
    if (offset + 5 <= bytes_ && data_[offset] == 'f') {
        out = false;
        return true;
    }
    return false;
}

bool Reader::read_string(const char* key, char* out, int capacity) const {
    int offset = value_offset(key);
    if (offset < 0 || data_[offset] != '"') return false;
    offset++;
    int count = 0;
    while (offset < bytes_ && data_[offset] != '"' && count < capacity - 1)
        out[count++] = data_[offset++];
    out[count] = 0;
    return true;
}

bool Writer::reserve(int extra) {
    if (length_ + extra > capacity_ - 2) {
        overflowed_ = true;
        return false;
    }
    return true;
}
void Writer::write_separator() {
    if (!first_) buffer_[length_++] = ',';
    first_ = false;
}
void Writer::write_text(const char* text) {
    while (*text) buffer_[length_++] = *text++;
}
void Writer::write_quoted(const char* text) {
    buffer_[length_++] = '"';
    while (*text) {
        if (*text == '"' || *text == '\\') buffer_[length_++] = '\\';
        buffer_[length_++] = *text++;
    }
    buffer_[length_++] = '"';
}
void Writer::add_integer(const char* key, int64_t value) {
    char digits[20];
    int digit_count = 0;
    bool negative = value < 0;
    uint64_t magnitude = negative ? 0 - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
    do {
        digits[digit_count++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    const int needed = (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 +
                       (negative ? 1 : 0) + digit_count;
    if (!reserve(needed)) return;
    write_separator();
    write_quoted(key);
    buffer_[length_++] = ':';
    if (negative) buffer_[length_++] = '-';
    while (digit_count > 0) buffer_[length_++] = digits[--digit_count];
}
void Writer::add_boolean(const char* key, bool value) {
    const char* text = value ? "true" : "false";
    const int needed = (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 +
                       static_cast<int>(std::strlen(text));
    if (!reserve(needed)) return;
    write_separator();
    write_quoted(key);
    buffer_[length_++] = ':';
    write_text(text);
}
void Writer::add_string(const char* key, const char* value) {
    int value_length = 0;
    int escapes = 0;
    for (const char* character = value; *character; character++) {
        value_length++;
        if (*character == '"' || *character == '\\') escapes++;
    }
    const int needed =
        (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 + value_length + escapes + 2;
    if (!reserve(needed)) return;
    write_separator();
    write_quoted(key);
    buffer_[length_++] = ':';
    write_quoted(value);
}
int Writer::finish() {
    if (length_ < capacity_ - 1)
        buffer_[length_++] = '}';
    else
        overflowed_ = true;
    buffer_[length_] = 0;
    return length_;
}

}
