// The callsign store alone: what a Type 66 registration frame leaves behind, keyed how, and for how
// long.
#include <string>

#include "core/traffic/callsigns.h"
#include "doctest/doctest.h"

using namespace skyblip;
using namespace skyblip::traffic;

namespace {

int filled_names(const CallsignTable& names) {
    int count = 0;
    for (int i = 0; i < CallsignTable::kMaxCallsigns; i++)
        if (names.find(58, static_cast<uint32_t>(0x1000 + i)) != nullptr) count++;
    return count;
}

}  // namespace

TEST_CASE("callsigns: a name is keyed on the address table as well as the address") {
    CallsignTable names;
    names.learn(58, 0x123456, "D-KXYZ", 1000);
    names.learn(6, 0x123456, "HB-1234", 1000);

    REQUIRE(names.find(58, 0x123456) != nullptr);
    CHECK(std::string(names.find(58, 0x123456)) == "D-KXYZ");
    CHECK(std::string(names.find(6, 0x123456)) == "HB-1234");
    CHECK(names.find(7, 0x123456) == nullptr);
    CHECK(names.find(58, 0x123457) == nullptr);
}

TEST_CASE("callsigns: hearing the same aircraft again refreshes rather than fills the table") {
    CallsignTable names;
    names.learn(58, 0x123456, "D-KXYZ", 1000);
    names.learn(58, 0x123456, "D-KABC", 1010);
    CHECK(std::string(names.find(58, 0x123456)) == "D-KABC");
    CHECK(names.heard_s(58, 0x123456) == 1010);
}

TEST_CASE("callsigns: a name outlives the target and then it is forgotten") {
    CallsignTable names;
    names.learn(58, 0x123456, "D-KXYZ", 1000);

    names.age_out(1000 + kCallsignForgetS);
    CHECK(names.find(58, 0x123456) != nullptr);
    names.age_out(1001 + kCallsignForgetS);
    CHECK(names.find(58, 0x123456) == nullptr);
}

// One name slot per target slot, so a full table is one that heard more aircraft than it can track.
TEST_CASE("callsigns: a full table drops the name heard longest ago") {
    CallsignTable names;
    for (int i = 0; i < CallsignTable::kMaxCallsigns; i++)
        names.learn(58, static_cast<uint32_t>(0x1000 + i), "FULL", static_cast<uint32_t>(100 + i));
    CHECK(filled_names(names) == CallsignTable::kMaxCallsigns);

    names.learn(58, 0x2000, "NEW", 500);
    CHECK(filled_names(names) == CallsignTable::kMaxCallsigns - 1);
    CHECK(names.find(58, 0x1000) == nullptr);
    CHECK(names.find(58, 0x1001) != nullptr);
    REQUIRE(names.find(58, 0x2000) != nullptr);
    CHECK(std::string(names.find(58, 0x2000)) == "NEW");
}

TEST_CASE("callsigns: an empty name is not a name") {
    CallsignTable names;
    names.learn(58, 0x123456, "", 1000);
    names.learn(58, 0x123457, nullptr, 1000);
    CHECK(names.find(58, 0x123456) == nullptr);
    CHECK(names.find(58, 0x123457) == nullptr);
}

// The wire field is 14 characters and nothing terminates them.
TEST_CASE("callsigns: a name the width of the wire field is kept whole") {
    CallsignTable names;
    names.learn(58, 0x123456, "ABCDEFGHIJKLMN", 1000);
    REQUIRE(names.find(58, 0x123456) != nullptr);
    CHECK(std::string(names.find(58, 0x123456)) == "ABCDEFGHIJKLMN");
}
