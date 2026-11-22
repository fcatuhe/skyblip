// The update frame names no product on any unit: the identity frame does, for every product alike.
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

TEST_CASE("product: neither a Go's update frame nor a Bravo's names a product") {
    Rig go;
    REQUIRE(go.setup() == Status::Ok);
    on_ground(go, go.now_ms);
    go.send("{\"command\":\"update\"}");
    go.run(go.now_ms, go.now_ms + 200);
    const std::string go_frame = go.last_on(events::Endpoint::Config);
    CHECK(go_frame.rfind("{\"command\":\"update\"", 0) == 0);
    CHECK(go_frame.find("\"product\"") == std::string::npos);

    BravoRig bravo;
    REQUIRE(bravo.setup() == Status::Ok);
    bravo.send("{\"command\":\"update\"}");
    bravo.run_for(200);
    const std::string bravo_frame = bravo.last_on(events::Endpoint::Config);
    CHECK(bravo_frame.rfind("{\"command\":\"update\"", 0) == 0);
    CHECK(bravo_frame.find("\"product\"") == std::string::npos);
}
