// A holder's Down lost to a full link_events queue still ends its claim, prompt and upload window.
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {
void taxi(Rig& rig, uint32_t seconds) { rig.seconds(seconds, 0, 300); }

void connect_two_as_holder_leaves(Rig& rig) {
    auto& link = rig.platform.link();
    link.raise_link(2);
    link.declare_payload_bytes(2, 182);
    link.raise_link(3);
    link.declare_payload_bytes(3, 182);
    link.drop_link(1);
    rig.run_for(200);
}
}  // namespace

// Two phones connect and settle their MTU as the holder leaves: its Down is the fifth of four
// slots.
TEST_CASE("companion link: a holder whose Down the queue dropped still gives up the unit") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, 3);
    auto& link = rig.platform.link();
    auto& config = rig.product.config_link().config();

    link.raise_link(1);
    rig.run_for(100);
    rig.send_from(1, "{\"command\":\"set\",\"callsign\":\"F-JABC\"}");
    rig.run_for(100);
    REQUIRE(config.claim_holder() == 1);
    REQUIRE(config.pending() == common::Pending::Set);

    connect_two_as_holder_leaves(rig);
    REQUIRE(rig.product.bus().link_events.dropped() == 1);
    CHECK_FALSE(config.claim().held());
    CHECK(config.pending() == common::Pending::None);
    CHECK(rig.product.screen().prompt() == common::Pending::None);

    link.raise_link(1);
    rig.run_for(100);
    link.clear();
    rig.send_from(1, "{\"command\":\"get\"}");
    rig.run_for(100);
    CHECK(rig.last_on(events::Endpoint::Config).find("claimed") == std::string::npos);
}

TEST_CASE("companion link: a holder whose Down the queue dropped leaves no upload window open") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, 20);
    auto& config = rig.product.config_link().config();

    rig.platform.link().raise_link(1);
    rig.run_for(100);
    rig.send_from(1, "{\"command\":\"dfu\",\"version\":\"0.2.0+15\"}");
    rig.run_for(3000);
    REQUIRE(config.pending() == common::Pending::Dfu);
    rig.press();
    rig.run_for(200);
    REQUIRE(config.upload_allowed());

    connect_two_as_holder_leaves(rig);
    REQUIRE(rig.product.bus().link_events.dropped() == 1);
    CHECK_FALSE(config.upload_allowed());
}
