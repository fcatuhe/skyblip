// The ten-minute upload window, against the MCUmgr thread that reads its gate between passes.
#include <string>

#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

bool sent_since(Rig& rig, size_t from, const std::string& frame) {
    const auto& sent = rig.platform.link().sent;
    for (size_t i = from; i < sent.size(); i++)
        if (link_text(sent[i].bytes) == frame) return true;
    return false;
}

}  // namespace

// The pass that closed the window had published the gate open, so the last chunk still landed.
TEST_CASE("product: an upload that finishes as its window expires is answered window_closed") {
    Rig rig;
    stage_versions(rig);
    REQUIRE(rig.setup() == Status::Ok);
    open_upload_window(rig);
    pass(rig);
    rig.run_for(10u * 60u * 1000u - 2000u);
    REQUIRE(config(rig).upload_allowed());
    const size_t before = rig.platform.link().sent.size();
    while (config(rig).upload_allowed()) pass(rig);
    REQUIRE(rig.platform.dfu().upload_allowed_published);

    rig.platform.dfu().finished_upload = true;
    rig.run_for(5000);
    CHECK_FALSE(rig.platform.dfu().upload_allowed_published);
    CHECK(rig.platform.dfu().triggered == 0);
    CHECK_FALSE(rig.product.shutdown().going_down());
    CHECK(sent_since(
        rig, before,
        "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"window_closed\"}"));
}
