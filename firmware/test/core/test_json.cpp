// The JSON the companion link speaks, with no heap: what is refused and what is left out.
#include <cstring>
#include <string>

#include "core/util/json.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("json: the reader parses integers, booleans and strings, the writer emits them") {
    const char* j = "{\"a\":42,\"b\":true,\"c\":\"hi\",\"d\":-7}";
    json::Reader reader(j, static_cast<int>(strlen(j)));
    long value;
    bool flag;
    char text[8];
    CHECK(reader.read_integer("a", value));
    CHECK(value == 42);
    CHECK(reader.read_boolean("b", flag));
    CHECK(flag);
    CHECK(reader.read_string("c", text, sizeof(text)));
    CHECK(std::string(text) == "hi");
    CHECK(reader.read_integer("d", value));
    CHECK(value == -7);
    CHECK_FALSE(reader.has("z"));

    char out[64];
    json::Writer writer(out, sizeof(out));
    writer.add_integer("x", 5);
    writer.add_boolean("y", false);
    writer.finish();
    CHECK(std::string(out) == "{\"x\":5,\"y\":false}");
}

TEST_CASE("json: a frame that ends on a colon has no value, and nothing past it is read") {
    const char frame[] = {'{', '"', 'c', 'm', 'd', '"', ':'};
    json::Reader reader(frame, static_cast<int>(sizeof(frame)));
    long value = 0;
    bool flag = false;
    char text[8];
    CHECK_FALSE(reader.read_integer("command", value));
    CHECK_FALSE(reader.read_boolean("command", flag));
    CHECK_FALSE(reader.read_string("command", text, sizeof(text)));
}

TEST_CASE("json: an integer past 32 bits is refused, so the host reads what the device reads") {
    const char* j =
        "{\"max\":2147483647,\"min\":-2147483648,\"over\":2147483648,\"under\":-2147483649,"
        "\"huge\":99999999999999999999999}";
    json::Reader reader(j, static_cast<int>(strlen(j)));
    long value = 0;
    CHECK(reader.read_integer("max", value));
    CHECK(value == 2147483647L);
    CHECK(reader.read_integer("min", value));
    CHECK(value == -2147483647L - 1);
    CHECK_FALSE(reader.read_integer("over", value));
    CHECK_FALSE(reader.read_integer("under", value));
    CHECK_FALSE(reader.read_integer("huge", value));
}

// core/comms's status reply is a fixed-size stack buffer with no heap behind
// it: a key that overruns it must never come out half-written. A writer that
// silently dropped the tail of its last key would still close the brace and
// look like valid, complete JSON to anything downstream.
TEST_CASE("json: a key that will not fit whole is left out, not cut short") {
    char out[16];
    json::Writer writer(out, sizeof(out));
    writer.add_integer("x", 5);
    CHECK_FALSE(writer.overflowed());

    writer.add_boolean("yy", true);  // does not fit in what is left of a 16-byte buffer
    CHECK(writer.overflowed());

    const int written = writer.finish();
    CHECK(std::string(out) == "{\"x\":5}");  // whole and valid, one key short
    CHECK(written == 7);
    CHECK(static_cast<int>(std::strlen(out)) == written);  // the reported length is real
}

TEST_CASE("json: a buffer sized for the exact worst case never overflows") {
    char out[29];
    json::Writer writer(out, sizeof(out));
    writer.add_string("k", "say \"hi\"");  // 1-char key, two escaped quotes in the value
    writer.add_boolean("b", false);
    const int written = writer.finish();
    CHECK_FALSE(writer.overflowed());
    CHECK(static_cast<int>(std::strlen(out)) == written);
    CHECK(std::string(out) == "{\"k\":\"say \\\"hi\\\"\",\"b\":false}");
}
