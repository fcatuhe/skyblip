// The CBOR the companion link speaks, with no heap: what is written, read and refused.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/comms/link_registry.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "test/support/link_vectors.h"

using namespace skyblip;

namespace {

std::vector<uint8_t> bytes_of(std::initializer_list<int> values) {
    std::vector<uint8_t> out;
    for (int value : values) out.push_back(static_cast<uint8_t>(value));
    return out;
}

std::vector<uint8_t> one_unsigned(uint64_t value) {
    uint8_t out[16];
    cbor::Writer writer(out, sizeof(out));
    writer.add_unsigned(0, value);
    return std::vector<uint8_t>(out, out + writer.finish());
}

std::vector<uint8_t> one_integer(int64_t value) {
    uint8_t out[16];
    cbor::Writer writer(out, sizeof(out));
    writer.add_integer(0, value);
    return std::vector<uint8_t>(out, out + writer.finish());
}

cbor::Reader reader_of(const std::vector<uint8_t>& frame) {
    return cbor::Reader(ConstByteSpan(frame.data(), frame.size()));
}

}  // namespace

// RFC 8949 Appendix A, each value behind the one-entry map {0: value} the writer opens.
TEST_CASE("cbor: integers are written in the shortest form RFC 8949 gives them") {
    CHECK(one_unsigned(0) == bytes_of({0xA1, 0x00, 0x00}));
    CHECK(one_unsigned(23) == bytes_of({0xA1, 0x00, 0x17}));
    CHECK(one_unsigned(24) == bytes_of({0xA1, 0x00, 0x18, 0x18}));
    CHECK(one_unsigned(1000) == bytes_of({0xA1, 0x00, 0x19, 0x03, 0xE8}));
    CHECK(one_unsigned(1000000) == bytes_of({0xA1, 0x00, 0x1A, 0x00, 0x0F, 0x42, 0x40}));
    CHECK(one_integer(-1) == bytes_of({0xA1, 0x00, 0x20}));
    CHECK(one_integer(-100) == bytes_of({0xA1, 0x00, 0x38, 0x63}));
    CHECK(one_integer(-1000) == bytes_of({0xA1, 0x00, 0x39, 0x03, 0xE7}));
}

TEST_CASE("cbor: an integer past 32 bits refuses the frame, which the link's grammar stops at") {
    CHECK(one_unsigned(UINT32_MAX) == bytes_of({0xA1, 0x00, 0x1A, 0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK(one_integer(-int64_t{UINT32_MAX} - 1) ==
          bytes_of({0xA1, 0x00, 0x3A, 0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK(one_unsigned(uint64_t{UINT32_MAX} + 1).empty());
    CHECK(one_integer(int64_t{UINT32_MAX} + 1).empty());
    CHECK(one_integer(-int64_t{UINT32_MAX} - 2).empty());
}

TEST_CASE("cbor: text that is not UTF-8 refuses the frame, so no reader is handed one") {
    uint8_t out[16];
    cbor::Writer valid(out, sizeof(out));
    valid.add_text(0, "\xC3\xA9t\xC3\xA9");
    CHECK(valid.finish() == 8);
    for (const char* malformed :
         {"\xC3(", "\xFF", "\x80", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"}) {
        CAPTURE(malformed);
        cbor::Writer writer(out, sizeof(out));
        writer.add_text(0, malformed);
        CHECK(writer.finish() == 0);
    }
}

TEST_CASE("cbor: booleans, text, byte strings and arrays are written as RFC 8949 spells them") {
    uint8_t out[32];
    cbor::Writer writer(out, sizeof(out));
    const uint8_t raw[] = {1, 2, 3, 4};
    const uint32_t counts[] = {1, 2, 3};
    writer.add_boolean(1, false);
    writer.add_boolean(2, true);
    writer.add_text(3, "IETF");
    writer.add_bytes(4, ConstByteSpan(raw, sizeof(raw)));
    writer.add_unsigned_array(5, counts, 3);
    const int written = writer.finish();
    CHECK(std::vector<uint8_t>(out, out + written) ==
          bytes_of({0xA5, 0x01, 0xF4, 0x02, 0xF5, 0x03, 0x64, 'I',  'E', 'T', 'F',
                    0x04, 0x44, 1,    2,    3,    4,    0x05, 0x83, 1,   2,   3}));
}

// A frame cut short would read as a whole one with fewer keys, so the writer refuses it all.
TEST_CASE("cbor: a key that will not fit whole refuses the frame, which is never cut short") {
    uint8_t out[6];
    cbor::Writer writer(out, sizeof(out));
    writer.add_unsigned(0, 5);
    CHECK_FALSE(writer.refused());
    writer.add_text(1, "too long");
    CHECK(writer.refused());
    CHECK(writer.finish() == 0);
}

TEST_CASE("cbor: a buffer sized for the exact frame takes it") {
    uint8_t out[7];
    cbor::Writer writer(out, sizeof(out));
    writer.add_unsigned(0, 5);
    writer.add_text(1, "ab");
    CHECK(writer.finish() == 7);
    CHECK_FALSE(writer.refused());
}

TEST_CASE("cbor: keys go out ascending, and one out of order refuses the frame") {
    uint8_t out[16];
    cbor::Writer writer(out, sizeof(out));
    writer.add_unsigned(2, 1);
    writer.add_unsigned(2, 1);
    CHECK(writer.finish() == 0);

    cbor::Writer backwards(out, sizeof(out));
    backwards.add_unsigned(3, 1);
    backwards.add_unsigned(1, 1);
    CHECK(backwards.finish() == 0);
}

// The diagnostics frames carry up to 23 keys, one short of the map head growing a byte.
TEST_CASE("cbor: a map of 24 entries grows its head to two bytes and stays readable") {
    uint8_t out[64];
    cbor::Writer writer(out, sizeof(out));
    for (uint32_t key = 0; key < 24; key++) writer.add_unsigned(key, key);
    const int written = writer.finish();
    REQUIRE(written == 2 + 24 * 2);
    CHECK(out[0] == 0xB8);
    CHECK(out[1] == 24);
    const cbor::Reader reader(ConstByteSpan(out, static_cast<size_t>(written)));
    REQUIRE(reader.valid());
    CHECK(reader.count() == 24);
    CHECK(reader.read_unsigned(23).value() == 23u);
}

TEST_CASE("cbor: the budget a report packs against matches what the writer spends") {
    cbor::MapBudget budget(7);
    CHECK(budget.take(cbor::unsigned_entry_bytes(0, 5)));
    CHECK(budget.take(cbor::text_entry_bytes(1, "ab")));
    CHECK_FALSE(budget.take(cbor::boolean_entry_bytes(2)));
}

TEST_CASE("cbor: the reader finds each key, and says absent, wrong type or too wide apart") {
    uint8_t out[48];
    cbor::Writer writer(out, sizeof(out));
    const uint8_t nonce[] = {9, 8, 7};
    writer.add_unsigned(0, 4);
    writer.add_integer(1, -7);
    writer.add_boolean(2, true);
    writer.add_text(3, "ABCD");
    writer.add_bytes(4, ConstByteSpan(nonce, sizeof(nonce)));
    writer.add_unsigned(5, UINT32_MAX);
    const cbor::Reader reader(ConstByteSpan(out, static_cast<size_t>(writer.finish())));
    REQUIRE(reader.valid());

    CHECK(reader.read_unsigned(0).value() == 4u);
    CHECK(reader.read_integer(1).value() == -7);
    CHECK(reader.read_boolean(2).value());
    char text[5];
    CHECK(reader.read_text(3, text, sizeof(text)) == Status::Ok);
    CHECK(std::string(text) == "ABCD");
    uint8_t read[3];
    CHECK(reader.read_bytes(4, ByteSpan(read, sizeof(read))) == Status::Ok);
    CHECK(std::memcmp(read, nonce, sizeof(nonce)) == 0);

    CHECK(reader.read_unsigned(9).status() == Status::NotFound);
    CHECK(reader.read_unsigned(1).status() == Status::Invalid);
    CHECK(reader.read_boolean(0).status() == Status::Invalid);
    CHECK(reader.read_integer(5).status() == Status::OutOfRange);
    char short_text[4];
    CHECK(reader.read_text(3, short_text, sizeof(short_text)) == Status::OutOfRange);
    uint8_t wider[4];
    CHECK(reader.read_bytes(4, ByteSpan(wider, sizeof(wider))) == Status::OutOfRange);
}

TEST_CASE("cbor: a signed read holds to 32 bits, as the device's own fields do") {
    CHECK(reader_of(one_integer(INT32_MAX)).read_integer(0).value() == INT32_MAX);
    CHECK(reader_of(one_integer(INT32_MIN)).read_integer(0).value() == INT32_MIN);
    CHECK(reader_of(one_integer(int64_t{INT32_MAX} + 1)).read_integer(0).status() ==
          Status::OutOfRange);
    CHECK(reader_of(one_integer(int64_t{INT32_MIN} - 1)).read_integer(0).status() ==
          Status::OutOfRange);
}

TEST_CASE("cbor: a frame holds its keys within a set of numbers, or names one outside it") {
    uint8_t out[16];
    cbor::Writer writer(out, sizeof(out));
    writer.add_unsigned(0, 1);
    writer.add_unsigned(3, 1);
    const cbor::Reader reader(ConstByteSpan(out, static_cast<size_t>(writer.finish())));
    CHECK(reader.keys_within(0b1001));
    CHECK_FALSE(reader.keys_within(0b0001));

    cbor::Writer past(out, sizeof(out));
    past.add_unsigned(64, 1);
    const cbor::Reader beyond(ConstByteSpan(out, static_cast<size_t>(past.finish())));
    CHECK_FALSE(beyond.keys_within(UINT64_MAX));
}

TEST_CASE("cbor: an array of counts is read item by item") {
    uint8_t out[16];
    cbor::Writer writer(out, sizeof(out));
    const uint32_t counts[] = {1, 300, 70000};
    writer.add_unsigned_array(1, counts, 3);
    const cbor::Reader reader(ConstByteSpan(out, static_cast<size_t>(writer.finish())));
    cbor::Entry entry;
    REQUIRE(reader.entry(0, entry));
    CHECK(entry.type == cbor::Type::Array);
    CHECK(cbor::Reader::array_item(entry, 0).value() == 1u);
    CHECK(cbor::Reader::array_item(entry, 1).value() == 300u);
    CHECK(cbor::Reader::array_item(entry, 2).value() == 70000u);
    CHECK(cbor::Reader::array_item(entry, 3).status() == Status::NotFound);
}

// One grammar for the three readers of the link: the firmware's, blip.py's and the pages'.
TEST_CASE("cbor: the reader refuses every frame the link's shared vectors say is not one") {
    const LinkVectors vectors;
    for (const std::vector<uint8_t>& frame : vectors.frames("not_frames")) {
        CAPTURE(frame.size());
        CHECK_FALSE(reader_of(frame).valid());
    }
    for (const std::vector<uint8_t>& frame : vectors.frames("no_command")) {
        CAPTURE(frame.size());
        const cbor::Reader reader = reader_of(frame);
        CHECK(reader.valid());
        CHECK_FALSE(reader.read_unsigned(link::kCommandKey).ok());
    }
}

// The deterministic subset is the only one a client writes, so anything else is not a frame.
TEST_CASE("cbor: the reader refuses what the deterministic subset does not allow") {
    CHECK(reader_of(bytes_of({0xA1, 0x00, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0x18, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x18, 0x00, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xBF, 0x00, 0x05, 0xFF})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0x7F, 0x61, 'a', 0xFF})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0xF9, 0x3C, 0x00})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0xF6})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0xC1, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0xA0})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0x81, 0x61, 'a'})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA2, 0x01, 0x05, 0x00, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA2, 0x01, 0x05, 0x01, 0x06})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x20, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0x05, 0x00})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA1, 0x00, 0x62, 'a'})).valid());
    CHECK_FALSE(reader_of(bytes_of({0xA2, 0x00, 0x05})).valid());
    CHECK_FALSE(reader_of(bytes_of({0x81, 0x00})).valid());
    CHECK_FALSE(reader_of(bytes_of({})).valid());
}

TEST_CASE("cbor: nothing is found in a frame the reader refused") {
    const cbor::Reader reader = reader_of(bytes_of({0xA1, 0x00, 0x18, 0x05}));
    CHECK(reader.count() == 0);
    CHECK(reader.read_unsigned(0).status() == Status::NotFound);
}
