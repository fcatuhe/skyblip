// The JSON the companion link speaks, with no heap: what is refused and what is left out.
#include <cstring>
#include <string>

#include "core/util/json_min.h"
#include "doctest/doctest.h"

using namespace skyblip;

TEST_CASE("json_min: the reader parses ints, bools and strings, the writer emits them") {
    const char* j = "{\"a\":42,\"b\":true,\"c\":\"hi\",\"d\":-7}";
    json::Reader r(j, static_cast<int>(strlen(j)));
    long v;
    bool b;
    char s[8];
    CHECK(r.get_int("a", v));
    CHECK(v == 42);
    CHECK(r.get_bool("b", b));
    CHECK(b);
    CHECK(r.get_str("c", s, sizeof(s)));
    CHECK(std::string(s) == "hi");
    CHECK(r.get_int("d", v));
    CHECK(v == -7);
    CHECK_FALSE(r.has("z"));

    char out[64];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    w.kv_bool("y", false);
    w.finish();
    CHECK(std::string(out) == "{\"x\":5,\"y\":false}");
}

TEST_CASE("json_min: a frame that ends on a colon has no value, and nothing past it is read") {
    const char frame[] = {'{', '"', 'c', 'm', 'd', '"', ':'};
    json::Reader r(frame, static_cast<int>(sizeof(frame)));
    long v = 0;
    bool b = false;
    char s[8];
    CHECK_FALSE(r.get_int("cmd", v));
    CHECK_FALSE(r.get_bool("cmd", b));
    CHECK_FALSE(r.get_str("cmd", s, sizeof(s)));
}

TEST_CASE("json_min: an integer past 32 bits is refused, so the host reads what the device reads") {
    const char* j =
        "{\"max\":2147483647,\"min\":-2147483648,\"over\":2147483648,\"under\":-2147483649,"
        "\"huge\":99999999999999999999999}";
    json::Reader r(j, static_cast<int>(strlen(j)));
    long v = 0;
    CHECK(r.get_int("max", v));
    CHECK(v == 2147483647L);
    CHECK(r.get_int("min", v));
    CHECK(v == -2147483647L - 1);
    CHECK_FALSE(r.get_int("over", v));
    CHECK_FALSE(r.get_int("under", v));
    CHECK_FALSE(r.get_int("huge", v));
}

// core/comms's status reply is a fixed-size stack buffer with no heap behind
// it: a key that overruns it must never come out half-written. A writer that
// silently dropped the tail of its last key would still close the brace and
// look like valid, complete JSON to anything downstream.
TEST_CASE("json_min: a key that will not fit whole is left out, not cut short") {
    char out[16];
    json::Writer w(out, sizeof(out));
    w.kv_int("x", 5);
    CHECK_FALSE(w.overflowed());

    w.kv_bool("yy", true);  // does not fit in what is left of a 16-byte buffer
    CHECK(w.overflowed());

    const int n = w.finish();
    CHECK(std::string(out) == "{\"x\":5}");  // whole and valid, one key short
    CHECK(n == 7);
    CHECK(static_cast<int>(std::strlen(out)) == n);  // the reported length is real
}

TEST_CASE("json_min: a buffer sized for the exact worst case never overflows") {
    char out[29];
    json::Writer w(out, sizeof(out));
    w.kv_str("k", "say \"hi\"");  // 1-char key, two escaped quotes in the value
    w.kv_bool("b", false);
    const int n = w.finish();
    CHECK_FALSE(w.overflowed());
    CHECK(static_cast<int>(std::strlen(out)) == n);
    CHECK(std::string(out) == "{\"k\":\"say \\\"hi\\\"\",\"b\":false}");
}
