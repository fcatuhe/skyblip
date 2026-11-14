// A Tango switched on again: Bravo's first health comes before its UTC, which is no loss (#193).
#include <cstdint>

#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/self_test.h"
#include "test/support/glass_text.h"
#include "test/support/paired_products.h"

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

    bool reads(const char* text, int scale = 1) {
        return reads_in(tango.go.product.screen().framebuffer(), text, 0, 0, go::kGlassWidth,
                        go::kGlassHeight, scale);
    }
};

}  // namespace

TEST_CASE("tango boot: Bravo's first health without UTC reads waiting, with no tone") {
    Booted booted;
    booted.tango.run_for(3000);
    REQUIRE(booted.tango.service().link() == go::BravoLink::Up);
    CHECK(booted.bravo() == comms::BravoState::Waiting);
    CHECK(booted.tones_since_boot() == 0);
    CHECK(booted.reads("WAIT FOR BRAVO"));
    CHECK_FALSE(booted.reads("NO BRAVO", 2));
    CHECK_FALSE(booted.reads("NO BRAVO"));
}

TEST_CASE("tango boot: a Bravo that never has UTC stays waiting, on the radar and the self-test") {
    Booted booted;
    booted.tango.run_for(3 * go::BravoService::kBootSettleMs);
    CHECK(booted.bravo() == comms::BravoState::Waiting);
    CHECK(booted.tones_since_boot() == 0);
    CHECK(booted.reads("WAIT FOR BRAVO"));

    booted.tango.go.show(booted.tango.now_ms, go::Page::SelfTest);
    booted.tango.run_for(1100);
    CHECK(reads_in(booted.tango.go.product.screen().framebuffer(), "WAIT", 0,
                   go::kSelfTestFirstRowY, go::kGlassWidth, go::kGlassHeight));
}
