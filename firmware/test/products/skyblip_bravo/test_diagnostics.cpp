// Bravo's status dump: the adsb line says where each frame the LR2021 handed up went.
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "products/skyblip_bravo/services/diagnostics.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

struct Capture {
    std::vector<std::string> lines;

    bool ready() const { return true; }
    void line(const char* text, int characters) {
        lines.emplace_back(text, static_cast<size_t>(characters));
    }
    std::string last(const char* prefix) const {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->rfind(prefix, 0) == 0) return *it;
        return std::string();
    }
};

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("bravo: the adsb line splits refused frames from the aircraft folded before the table") {
    BravoRig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds_with_fix(1);
    AdsbAir corrupted = adsb_air(adsb_airborne_position(0x3C6586, 0, 48.51, 8.5, false, 1800));
    corrupted.chips[10] ^= 0x0C;
    rig.platform.radio().receive_air(corrupted.chips, sizeof(corrupted.chips));
    rig.hear(adsb_frame(0x3C6586, 0, 28));
    rig.hear_aircraft(0x3C6586, 2000);
    rig.hear_aircraft(0x4CA2B1, 3000);
    rig.seconds_with_fix(1);

    bravo::DiagnosticsDump<Bravo, Capture> dump;
    dump.step(rig.product, rig.now_ms);
    const std::string adsb = dump.sink().last("adsb ");
    CHECK(has(adsb, " crc=1 unsupported=1 folded=4 aircraft=2"));
    CHECK_FALSE(has(adsb, "invalid="));
}
