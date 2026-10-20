// core/util/json.h: a tiny, allocation-free reader for FLAT JSON
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

}

#endif
