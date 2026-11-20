// A Tango switched on again: Bravo's first health comes before its UTC, which is no loss (#193).
#include <cstdint>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/self_test.h"
#include "test/support/glass_text.h"
#include "test/support/paired_products.h"
#include "test/support/radar_rig.h"

using namespace skyblip;

namespace {

PairedProducts::Stored paired_once() {
    PairedProducts first;
    first.pair_and_store();
    REQUIRE(first.go_pinned());
    REQUIRE(first.peer().paired());
    return first.stored();
}

struct Booted {
    PairedProducts tango{paired_once()};
    uint32_t tones{tango.go.platform.annunciator().tone_commands()};

    Booted() {
        tango.go.long_touch(tango.now_ms);
        REQUIRE(tango.go.product.screen().page() == go::Page::Radar);
    }

    comms::BravoState bravo() { return tango.go.state().bravo; }
    uint32_t tones_since_boot() { return tango.go.platform.annunciator().tone_commands() - tones; }

    const ui::Canvas& glass() { return tango.go.product.screen().framebuffer(); }
};

}  // namespace

TEST_CASE("tango boot: Bravo's first health without UTC reads waiting, with no tone" *
          doctest::test_suite("main")) {
    Booted booted;
    booted.tango.run_for(3000);
    REQUIRE(booted.tango.service().link() == go::BravoLink::Up);
    CHECK(booted.bravo() == comms::BravoState::Waiting);
    CHECK(booted.tones_since_boot() == 0);
    CHECK(go::corner_reads(booted.glass(), "BRAVO"));
    CHECK(go::corner_reads(booted.glass(), "MOVE OUT"));
}

TEST_CASE("tango boot: a Bravo that never has UTC stays waiting, on the radar and the self-test") {
    Booted booted;
    booted.tango.run_for(3 * go::BravoService::kBootSettleMs);
    CHECK(booted.bravo() == comms::BravoState::Waiting);
    CHECK(booted.tones_since_boot() == 0);
    CHECK(go::corner_reads(booted.glass(), "MOVE OUT"));

    booted.tango.go.show(booted.tango.now_ms, go::Page::SelfTest);
    booted.tango.run_for(1100);
    CHECK(reads_in(booted.tango.go.product.screen().framebuffer(), "WAIT", 0,
                   go::kSelfTestFirstRowY, go::kGlassWidth, go::kGlassHeight));
}

// The pilot can still fix a Bravo on the taxiway, so that is where the Go starts counting on it.
TEST_CASE("tango boot: a Bravo still without UTC when the Go taxis is lost, with one tone") {
    Booted booted;
    booted.tango.run_for(go::BravoService::kBootSettleMs);
    booted.tango.go.seconds(booted.tango.now_ms, 3, 0, 900);
    REQUIRE(booted.bravo() == comms::BravoState::Waiting);
    const uint32_t parked_tones = booted.tones_since_boot();

    booted.tango.go.seconds(booted.tango.now_ms, 2, 5000, 900);
    CHECK(booted.bravo() == comms::BravoState::NoUtc);
    CHECK(booted.tones_since_boot() == parked_tones + 1);
    CHECK(go::corner_reads(booted.glass(), "MOVE OUT", false));

    booted.tango.go.seconds(booted.tango.now_ms, 3, 0, 900);
    CHECK(booted.bravo() == comms::BravoState::NoUtc);
    CHECK(booted.tones_since_boot() == parked_tones + 1);
}
