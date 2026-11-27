// A Down names the connection that left, so it spares the next phone on the same session id.
#include "core/events/link.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

// The old connection leaves after config's drain, and the new one claims the log in that pass.
TEST_CASE(
    "companion link: a Down for the last connection on a reused session id leaves the new one "
    "its claim") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.seconds(3, 0, 300);
    auto& link = rig.platform.link();
    common::ConfigService& config = rig.product.config_link().config();

    rig.raise_link(1);
    rig.run_for(100);
    const uint32_t first = link.connection(1);

    rig.now_ms += 50;
    rig.platform.clock().set_milliseconds(rig.now_ms);
    rig.product.config_link().tick(rig.now_ms);
    rig.drop_link(1);
    rig.raise_link(1);
    const uint32_t second = link.connection(1);
    REQUIRE(second != first);
    rig.send_log_from(1, "{\"command\":\"list\"}");
    rig.product.flight_log().tick(rig.now_ms);
    REQUIRE(config.claim().peer() == events::Peer{1, second});

    rig.run_for(100);
    CHECK(config.claim().held());
    CHECK(config.claim().peer() == events::Peer{1, second});
}
