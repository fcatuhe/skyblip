// The version releases name this stack by, which lib/radio_protocols.rb reads as major.minor.patch.
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/version.h"

namespace {
bool is_triple(const char* text) {
    int dots = 0;
    bool digit = false;
    for (const char* c = text; *c != 0; c++) {
        if (*c == '.') {
            if (!digit) return false;
            dots++;
            digit = false;
        } else if (*c >= '0' && *c <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return dots == 2 && digit;
}
}  // namespace

TEST_CASE(
    "adsl version: the version is major.minor.patch, beside the specification it implements") {
    CHECK(is_triple(skyblip::adsl::kVersion));
    CHECK_FALSE(is_triple("1.0"));
    CHECK_FALSE(is_triple("1..0"));
    CHECK(std::strlen(skyblip::adsl::kSpecification) > 0);
}
