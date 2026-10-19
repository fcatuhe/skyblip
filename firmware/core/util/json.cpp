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

bool Reader::get_int(const char* key, long& out) const {
    int64_t v = 0;
    if (!get_number(key, v) || v < INT32_MIN || v > INT32_MAX) return false;
    out = static_cast<long>(v);
    return true;
}

bool Reader::get_uint(const char* key, uint32_t& out) const {
    int64_t v = 0;
    if (!get_number(key, v) || v < 0 || v > UINT32_MAX) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool Reader::get_number(const char* key, int64_t& out) const {
    int o = value_offset(key);
    if (o < 0) return false;
    bool neg = false;
    if (data_[o] == '-') {
        neg = true;
        o++;
    }
    if (o >= bytes_ || data_[o] < '0' || data_[o] > '9') return false;
    int64_t v = 0;
    while (o < bytes_ && data_[o] >= '0' && data_[o] <= '9') {
        v = v * 10 + (data_[o++] - '0');
        if (v > UINT32_MAX) return false;
    }
    out = neg ? -v : v;
    return true;
}

bool Reader::get_bool(const char* key, bool& out) const {
    int o = value_offset(key);
    if (o < 0) return false;
    if (o + 4 <= bytes_ && data_[o] == 't') {
        out = true;
        return true;
    }
    if (o + 5 <= bytes_ && data_[o] == 'f') {
        out = false;
        return true;
    }
    return false;
}

bool Reader::get_string(const char* key, char* out, int capacity) const {
    int o = value_offset(key);
    if (o < 0 || data_[o] != '"') return false;
    o++;
    int n = 0;
    while (o < bytes_ && data_[o] != '"' && n < capacity - 1) out[n++] = data_[o++];
    out[n] = 0;
    return true;
}

bool Writer::reserve(int extra) {
    if (length_ + extra > capacity_ - 2) {
        overflowed_ = true;
        return false;
    }
    return true;
}
void Writer::sep() {
    if (!first_) buffer_[length_++] = ',';
    first_ = false;
}
void Writer::raw(const char* s) {
    while (*s) buffer_[length_++] = *s++;
}
void Writer::raw_str(const char* s) {
    buffer_[length_++] = '"';
    while (*s) {
        if (*s == '"' || *s == '\\') buffer_[length_++] = '\\';
        buffer_[length_++] = *s++;
    }
    buffer_[length_++] = '"';
}
void Writer::add_integer(const char* key, int64_t v) {
    char temporary[20];
    int t = 0;
    bool neg = v < 0;
    uint64_t uv = neg ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    do {
        temporary[t++] = static_cast<char>('0' + uv % 10);
        uv /= 10;
    } while (uv);
    const int need =
        (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 + (neg ? 1 : 0) + t;
    if (!reserve(need)) return;
    sep();
    raw_str(key);
    buffer_[length_++] = ':';
    if (neg) buffer_[length_++] = '-';
    while (t > 0) buffer_[length_++] = temporary[--t];
}
void Writer::add_boolean(const char* key, bool v) {
    const char* val = v ? "true" : "false";
    const int need = (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 +
                     static_cast<int>(std::strlen(val));
    if (!reserve(need)) return;
    sep();
    raw_str(key);
    buffer_[length_++] = ':';
    raw(val);
}
void Writer::add_string(const char* key, const char* v) {
    int value_length = 0;
    int escapes = 0;
    for (const char* p = v; *p; p++) {
        value_length++;
        if (*p == '"' || *p == '\\') escapes++;
    }
    const int need =
        (first_ ? 0 : 1) + static_cast<int>(std::strlen(key)) + 2 + 1 + value_length + escapes + 2;
    if (!reserve(need)) return;
    sep();
    raw_str(key);
    buffer_[length_++] = ':';
    raw_str(v);
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
