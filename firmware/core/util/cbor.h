#ifndef SKYBLIP_CORE_UTIL_CBOR_H
#define SKYBLIP_CORE_UTIL_CBOR_H

#include <cstdint>

#include "core/util/result.h"
#include "core/util/span.h"

namespace skyblip::cbor {

enum class Major : uint8_t {
    Unsigned = 0,
    Negative = 1,
    Bytes = 2,
    Text = 3,
    Array = 4,
    Map = 5,
    Tag = 6,
    Simple = 7
};

constexpr uint8_t kFalse = 0xF4;
constexpr uint8_t kTrue = 0xF5;
constexpr int kMaxEntries = 255;

int head_bytes(uint64_t argument);
int text_bytes(const char* text);

int unsigned_entry_bytes(uint32_t key, uint64_t value);
int integer_entry_bytes(uint32_t key, int64_t value);
int boolean_entry_bytes(uint32_t key);
int text_entry_bytes(uint32_t key, const char* text);
int bytes_entry_bytes(uint32_t key, size_t bytes);
int unsigned_array_entry_bytes(uint32_t key, const uint32_t* values, int count);

class MapBudget {
   public:
    explicit MapBudget(int payload_bytes) : payload_bytes_(payload_bytes) {}

    bool take(int entry_bytes);

   private:
    int payload_bytes_;
    int used_{0};
    int entries_{0};
};

class Writer {
   public:
    Writer(uint8_t* out, int capacity);

    void add_unsigned(uint32_t key, uint64_t value);
    void add_integer(uint32_t key, int64_t value);
    void add_boolean(uint32_t key, bool value);
    void add_text(uint32_t key, const char* text);
    void add_bytes(uint32_t key, ConstByteSpan bytes);
    void add_unsigned_array(uint32_t key, const uint32_t* values, int count);
    template <class Code>
    void add_code(uint32_t key, Code code) {
        add_unsigned(key, static_cast<uint64_t>(code));
    }

    int finish();
    bool refused() const { return refused_; }

   private:
    bool open_entry(uint32_t key, int entry_bytes);
    void write_head(Major major, uint64_t argument);

    uint8_t* out_;
    int capacity_;
    int length_{1};
    int entries_{0};
    int64_t last_key_{-1};
    bool refused_{false};
};

enum class Type : uint8_t { Unsigned, Negative, Bytes, Text, Array, Boolean };

struct Entry {
    uint32_t key{0};
    Type type{Type::Unsigned};
    uint64_t argument{0};
    ConstByteSpan content{};
};

class Reader {
   public:
    explicit Reader(ConstByteSpan frame);

    bool valid() const { return valid_; }
    int count() const { return valid_ ? count_ : 0; }
    bool entry(int index, Entry& out) const;
    bool keys_within(uint64_t key_bits) const;

    Result<uint32_t> read_unsigned(uint32_t key) const;
    Result<int32_t> read_integer(uint32_t key) const;
    Result<bool> read_boolean(uint32_t key) const;
    Status read_text(uint32_t key, char* out, int capacity) const;
    Status read_bytes(uint32_t key, ByteSpan out) const;

    static Result<uint32_t> array_item(const Entry& array, int index);

   private:
    bool find(uint32_t key, Entry& out) const;

    ConstByteSpan frame_;
    size_t first_entry_{0};
    int count_{0};
    bool valid_{false};
};

}  // namespace skyblip::cbor

#endif
