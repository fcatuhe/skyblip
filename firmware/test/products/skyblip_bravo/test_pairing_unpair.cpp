// A Go unpairs on a link that is up, and Bravo hears it, whatever Bravo has said on it yet.
#include "doctest/doctest.h"
#include "test/support/paired_link.h"
#include "test/support/paired_products.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;

}  // namespace

// The Go reads its link up from Bravo's first health, so an unpair before it said no farewell.
TEST_CASE("bravo pairing: a Go that unpairs before Bravo's first health on a reconnect unpins it") {
    PairedProducts tango;
    tango.pair_and_store();
    REQUIRE(tango.peer().paired());

    tango.air.set_flow(Direction::ToCentral, Flow::Lost);
    tango.air.drop();
    for (int pass = 0; pass < 200 && !tango.go.platform.central().up(); pass++) tango.run_for(50);
    REQUIRE(tango.go.platform.central().up());
    REQUIRE(tango.service().link() == go::BravoLink::Down);
    REQUIRE(tango.peer().paired());

    tango.go.show(go::Page::Bravo);
    tango.go.press();
    REQUIRE(tango.go.product.screen().bravo_page().unpairing());
    tango.go.press();
    REQUIRE_FALSE(tango.go_pinned());
    tango.air.set_flow(Direction::ToCentral, Flow::Flowing);
    tango.run_for(1000);
    CHECK_FALSE(tango.peer().paired());
    CHECK_FALSE(tango.peer().requested());
    CHECK_FALSE(tango.service().leaving());
    CHECK_FALSE(tango.go.platform.central().up());
}
