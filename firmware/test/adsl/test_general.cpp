// ADS-L 4 SRD-860 issue 2 Subpart A and the performance appendix: what the document asks of a
// product.
#include <cstdint>

#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;

// TODO: fc 18sep26 a manufacturer's duty: the DoC template in the appendix, and the CE file
TEST_CASE("ADS-L.4.SRD860.A.4: the manufacturer declares conformity and CE marks the product" *
          doctest::skip()) {
    FAIL("paperwork: no declaration of conformity is filed for this firmware yet");
}

// TODO: fc 18sep26 a manufacturer's duty: installation and user manuals shipped with the product
TEST_CASE("ADS-L.4.SRD860.A.5: the product ships with an installation and a user manual" *
          doctest::skip()) {
    FAIL("paperwork: neither manual exists yet");
}

// The appendix's airborne transmitter: 12 to 14 dBm e.r.p., and half a second of latency.
TEST_CASE("ADS-L.4.SRD860.APPENDIX: the transmitter sits inside the nominal power and latency") {
    CHECK(parts::sx1262::kResultingErpCentiDb >= 1200);
    CHECK(parts::sx1262::kResultingErpCentiDb <= 1400);
    CHECK(timing::Transmitter::kFixMaxAgeMs <= 500);
}
