// A Go's update frame stays as narrow as it was: only another product names itself.
#include <string>

#include "doctest/doctest.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

TEST_CASE(
    "product: a Go's update frame names no product, which every reader takes for skyblip_go") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 0;
    on_ground(rig, now_ms);
    rig.send("{\"command\":\"update\"}");
    rig.run(now_ms, now_ms + 200);
    const std::string frame = rig.last_on(events::Endpoint::Config);
    CHECK(frame.rfind("{\"command\":\"update\"", 0) == 0);
    CHECK(frame.find("\"product\"") == std::string::npos);
}
