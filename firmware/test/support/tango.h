// Harness, not a test: a whole Go and the Bravo stub over a simulated link, a Tango.
#ifndef SKYBLIP_TEST_SUPPORT_TANGO_H
#define SKYBLIP_TEST_SUPPORT_TANGO_H

#include <cstdint>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "products/skyblip_go/pages/bravo.h"
#include "test/support/bravo_stub.h"
#include "test/support/paired_link.h"
#include "test/support/product_rig.h"

namespace skyblip {

struct Tango {
    BravoStub bravo;
    Rig go;
    PairedLink air{bravo.link(), go.platform.central()};
    uint32_t now_ms{100};

    explicit Tango(std::vector<BravoStub::Recorded> recording = {}) : bravo(std::move(recording)) {
        start();
    }

    Tango(std::vector<BravoStub::Recorded> recording, BarometerByHand barometer)
        : bravo(std::move(recording)), go(barometer) {
        start();
    }

    go::BravoService& service() { return go.product.bravo(); }

    void run_for(uint32_t span_ms) {
        go.run(now_ms, now_ms + span_ms);
        now_ms += span_ms + 50;
    }

    void open_page() {
        go.show(now_ms, go::Page::Bravo);
        run_for(300);
    }

    void pair() {
        open_page();
        REQUIRE(service().found_count() == 1);
        go.press(now_ms);
        REQUIRE(go.product.screen().bravo_page().confirming());
        go.press(now_ms);
        run_for(2000);
    }

    // INFO: fc 11nov26 the settings write waits for a durable-write window, forced after 3 s
    void pair_and_store() {
        pair();
        run_for(5000);
    }

   private:
    void start() {
        REQUIRE(go.setup() == Status::Ok);
        go.world = [this](uint32_t at_ms) {
            bravo.run_pass(at_ms);
            air.pump();
        };
    }
};

}  // namespace skyblip

#endif
