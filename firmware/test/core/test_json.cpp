// The flat JSON the scenarios and the activation vectors are read from, with no heap.
#include <cstring>
#include <string>

#include "core/util/json.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("json: the reader parses integers, booleans and strings") {
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
