// core/util/json.h: a tiny, allocation-free reader/writer for FLAT JSON
#ifndef SKYBLIP_CORE_UTIL_JSON_H
#define SKYBLIP_CORE_UTIL_JSON_H

#include <cstddef>
#include <cstdint>

namespace skyblip::json {

class Reader {
   public:
    Reader(const char* data, int bytes) : data_(data), bytes_(bytes) {}

    bool read_integer(const char* key, long& out) const;
    bool read_unsigned(const char* key, uint32_t& out) const;
    bool read_boolean(const char* key, bool& out) const;
    bool read_string(const char* key, char* out, int capacity) const;
    bool has(const char* key) const;

   private:
    const char* data_;
    int bytes_;
    int value_offset(const char* key) const;
    bool read_number(const char* key, int64_t& out) const;
};

// Every call either commits a complete key and value, or commits nothing at
// all: a key that will not fit whole is left out rather than cut short, and
// overflowed() then reads true. A caller that never sizes its buffer wrong
// never sees this; a caller that does gets a short, valid, checkable object
// instead of one with a mangled last field nobody asked for.
class Writer {
   public:
    Writer(char* out, int capacity) : buffer_(out), capacity_(capacity) {
        buffer_[length_++] = '{';
    }
    void add_integer(const char* key, int64_t value);
    void add_boolean(const char* key, bool value);
    void add_string(const char* key, const char* value);
    int finish();
    int length() const { return length_; }
    bool overflowed() const { return overflowed_; }

   private:
    bool reserve(int extra);
    void write_separator();
    void write_text(const char* text);
    void write_quoted(const char* text);
    char* buffer_;
    int capacity_;
    int length_{0};
    bool first_{true};
    bool overflowed_{false};
};

}

#endif
