#include "simulator/world/json.h"

#include <cstring>

namespace skyblip::simulator::json {

namespace {
bool is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}

int Reader::value_offset(const char* key) const {
    int key_length = static_cast<int>(std::strlen(key));
    int i = 0;
    int key_start = 0;
    int key_end = 0;
    while (next_key(i, key_start, key_end)) {
        bool match = (key_end - key_start == key_length);
        for (int j = 0; match && j < key_length; j++)
            if (data_[key_start + j] != key[j]) match = false;
        if (match) return i < bytes_ ? i : -1;
    }
    return -1;
}

int Reader::key_count() const {
    int count = 0;
    int i = 0;
    int key_start = 0;
    int key_end = 0;
    while (next_key(i, key_start, key_end)) count++;
    return count;
}

bool Reader::next_key(int& i, int& key_start, int& key_end) const {
    while (i < bytes_) {
        if (data_[i] != '"') {
            i++;
            continue;
        }
        key_start = i + 1;
        key_end = key_start;
        while (key_end < bytes_ && data_[key_end] != '"') key_end++;
        if (key_end >= bytes_) return false;
        i = key_end + 1;
        while (i < bytes_ && is_whitespace(data_[i])) i++;
        if (i < bytes_ && data_[i] == ':') {
            i++;
            while (i < bytes_ && is_whitespace(data_[i])) i++;
            return true;
        }
    }
    return false;
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

}
