// A phone whose Up a full link_events queue dropped still counts as connected.
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {
void taxi(Rig& rig, uint32_t seconds) { rig.seconds(seconds, 0, 300); }

std::string last_config_frame(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
        if (it->endpoint == events::Endpoint::Config) return link_text(it->bytes);
    return std::string();
}
}  // namespace

// Two phones connect and settle their MTU in one pass: the third phone's Up is the fifth event.
TEST_CASE("companion link: a phone whose Up the queue dropped still hears the gauge") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    rig.platform.battery().divider_mv = 3900;
    taxi(rig, 4);
    auto& link = rig.platform.link();

    link.raise_link(1);
    link.declare_payload_bytes(1, 182);
    link.raise_link(2);
    link.declare_payload_bytes(2, 182);
    link.raise_link(3);
    REQUIRE(link.links() == 3);
    rig.run_for(100);
    REQUIRE(rig.product.bus().link_events.dropped() == 1);

    link.drop_link(1);
    link.drop_link(2);
    rig.run_for(100);
    CHECK(rig.link_up());

    link.clear();
    rig.platform.battery().divider_mv = 3600;
    taxi(rig, 4);
    CHECK(last_config_frame(rig).find("\"battery_percent\"") != std::string::npos);
}
