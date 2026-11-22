// Each reply frame leaves the unit once, the pending acknowledgement and every part of a dump.
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"

using namespace skyblip;

namespace {

std::vector<std::string> config_frames(Rig& rig) {
    std::vector<std::string> out;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == events::Endpoint::Config) out.push_back(link_text(frame.bytes));
    return out;
}

int count_of(const std::vector<std::string>& frames, const std::string& text) {
    int count = 0;
    for (const std::string& frame : frames)
        if (frame.find(text) != std::string::npos) count++;
    return count;
}

}  // namespace

// The pinned case: blip.py read 2073's second pending acknowledgement as the verdict and hung up.
TEST_CASE("link: a pressed command is answered pending once, and a dump sends each part once") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.now_ms = 100;
    taxi(rig, rig.now_ms, 5);
    rig.platform.link().declare_payload_bytes(64);
    go::DiagnosticsCollector<Go>().step(rig.product, rig.now_ms);
    rig.platform.link().sent.clear();

    rig.send("{\"command\":\"diagnostics\"}");
    taxi(rig, rig.now_ms, 3);
    const std::vector<std::string> dump = config_frames(rig);
    REQUIRE(count_of(dump, "\"more\":true") > 1);
    for (size_t at = 1; at < dump.size(); at++) {
        CAPTURE(dump[at]);
        CHECK(dump[at] != dump[at - 1]);
    }
    CHECK(count_of(dump, "\"more\":false") == 1);

    rig.platform.link().sent.clear();
    rig.send("{\"command\":\"capture\",\"profile\":\"full\"}");
    taxi(rig, rig.now_ms, 3);
    CHECK(count_of(config_frames(rig), "\"pending\":true") == 1);
}
