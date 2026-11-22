#ifndef SKYBLIP_PROTOCOLS_COMMON_VERSION_TRIPLE_H
#define SKYBLIP_PROTOCOLS_COMMON_VERSION_TRIPLE_H

namespace skyblip::wire {

constexpr bool is_version_triple(const char* text) {
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

static_assert(is_version_triple("10.2.0"));
static_assert(!is_version_triple("1.0"));
static_assert(!is_version_triple("1..0"));
static_assert(!is_version_triple("1.0.0-rc1"));

}

#endif
