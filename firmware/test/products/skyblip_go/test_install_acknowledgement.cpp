// The install acknowledgement against a link short of notification buffers.
#include <string>

#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

bool told_since(Rig& rig, size_t from, const char* needle) {
    const auto& sent = rig.platform.link().sent;
    for (size_t i = from; i < sent.size(); i++)
        if (link_text(sent[i].bytes).find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

// The pass that accepts the upload starts the park, and the park stops the link's own tick.
TEST_CASE("product: an install acknowledgement the link held reaches the phone before the swap") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    open_upload_window(rig);
    pass(rig);
    const uint16_t session = rig.platform.link().session_id();
    const size_t before = rig.platform.link().sent.size();

    rig.platform.link().hold_session(session, true);
    land_upload(rig);
    REQUIRE(rig.product.shutdown().reason() == power::ShutdownReason::Install);
    CHECK_FALSE(told_since(rig, before, "\"reason\":\"install\""));
    rig.platform.link().hold_session(session, false);

    while (rig.platform.dfu().triggered == 0 && !told_since(rig, before, "\"reason\":\"install\""))
        pass(rig);
    CHECK(told_since(rig, before, "\"reason\":\"install\""));
    CHECK(rig.platform.dfu().triggered == 0);

    rig.run(rig.now_ms, rig.now_ms + power::kParkSettleMs + power::kReleaseSettleMs + 500);
    CHECK(rig.platform.dfu().triggered == 1);
}
