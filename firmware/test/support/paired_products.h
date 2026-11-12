// Harness, not a test: a whole Go and a whole Bravo over the simulated link, both on the world's
// clock.
#ifndef SKYBLIP_TEST_SUPPORT_PAIRED_PRODUCTS_H
#define SKYBLIP_TEST_SUPPORT_PAIRED_PRODUCTS_H

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/page.h"
#include "test/support/bravo_rig.h"
#include "test/support/paired_link.h"
#include "test/support/product_rig.h"

namespace skyblip {

struct PairedProducts {
    Rig go;
    BravoRig bravo{};
    PairedLink air{bravo.platform.link(), go.platform.central()};
    uint32_t now_ms{100};
    // Every frame the Go wrote, in order, whatever the air did with it.
    std::vector<std::string> go_wrote{};

    explicit PairedProducts(ports::Capabilities go_fitted = platform::host::Platform::kFullyFitted)
        : go(go_fitted) {
        REQUIRE(go.setup() == Status::Ok);
        REQUIRE(bravo.setup() == Status::Ok);
        go.world = [this](uint32_t at_ms) {
            for (const std::string& frame : go.platform.central().writes) go_wrote.push_back(frame);
            bravo.now_ms = at_ms;
            bravo.step();
            air.pump();
        };
    }

    go::BravoService& service() { return go.product.bravo(); }
    bravo::PeerService& peer() { return bravo.product.peer(); }

    void run_for(uint32_t span_ms) {
        go.run(now_ms, now_ms + span_ms);
        now_ms += span_ms + 50;
    }

    // The pilot opens the BRAVO page on the Go, picks the one listed and confirms it.
    void ask() {
        go.show(now_ms, go::Page::Bravo);
        run_for(300);
        REQUIRE(service().found_count() == 1);
        go.press(now_ms);
        go.press(now_ms);
        run_for(2000);
    }

    void press_bravo() {
        bravo.platform.set_button_down(true);
        run_for(150);
        bravo.platform.set_button_down(false);
        run_for(150);
    }

    // INFO: fc 11nov26 each side's settings write waits for a write window, forced after 3 s
    void pair_and_store() {
        ask();
        press_bravo();
        run_for(5000);
    }

    bool go_pinned() { return go::paired(go.settings()); }
};

}  // namespace skyblip

#endif
