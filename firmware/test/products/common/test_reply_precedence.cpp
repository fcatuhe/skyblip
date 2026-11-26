// A busy link sends the claim holder's verdict before a refusal waiting for another phone.
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

void taxi(Rig& rig, uint32_t seconds) { rig.seconds(seconds, 0, 300); }

bool told(Rig& rig, uint16_t session, const std::string& text) {
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == events::Endpoint::Config && frame.session == session &&
            link_text(frame.bytes).find(text) != std::string::npos)
            return true;
    return false;
}

}  // namespace

// The settings changed while the page waited in "confirming" for a verdict dropped behind the
// refusal.
TEST_CASE("companion link: the holder hears its verdict while a refusal waits for another phone") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, 6);
    auto& link = rig.platform.link();
    rig.raise_link(1);
    rig.raise_link(2);
    rig.run_for(100);
    rig.send_from(1, "{\"command\":\"set\",\"callsign\":\"F-JABC\"}");
    rig.run_for(3000);
    REQUIRE(rig.product.config_link().config().pending() == common::Pending::Set);

    link.hold_session(2, true);
    rig.send_from(2, "{\"command\":\"get\"}");
    rig.run_for(100);
    REQUIRE(rig.product.config_link().config().replying());
    link.clear();
    rig.press();
    rig.run_for(1000);
    REQUIRE(std::string(rig.settings().callsign) == "F-JABC");
    CHECK(told(rig, 1, "\"acknowledge\":true"));

    link.hold_session(2, false);
    rig.run_for(100);
    CHECK(told(rig, 2, "\"reason\":\"claimed\""));
}

TEST_CASE("companion link: the holder's verdict the link refused takes the place of the refusal") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    taxi(rig, 6);
    auto& link = rig.platform.link();
    rig.raise_link(1);
    rig.raise_link(2);
    rig.run_for(100);
    rig.send_from(1, "{\"command\":\"set\",\"callsign\":\"F-JABC\"}");
    rig.run_for(3000);
    REQUIRE(rig.product.config_link().config().pending() == common::Pending::Set);

    link.hold_session(2, true);
    rig.send_from(2, "{\"command\":\"get\"}");
    rig.run_for(100);
    link.hold_session(1, true);
    link.clear();
    rig.press();
    rig.run_for(500);
    REQUIRE(std::string(rig.settings().callsign) == "F-JABC");
    link.hold_session(1, false);
    link.hold_session(2, false);
    rig.run_for(100);
    CHECK(told(rig, 1, "\"acknowledge\":true"));
    CHECK_FALSE(told(rig, 2, "\"reason\":\"claimed\""));
    CHECK(rig.product.config_link().config().link_drops() == 1);
}
