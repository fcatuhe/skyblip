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
    FAIL("paperwork: the declaration is drafted on skyblip.eu/ads-l/conformity and not signed yet");
}

// TODO: fc 18sep26 a manufacturer's duty: installation and user manuals shipped with the product
TEST_CASE("ADS-L.4.SRD860.A.5: the product ships with an installation and a user manual" *
          doctest::skip()) {
    FAIL("paperwork: the manuals are the guides on skyblip.eu, which no host case can read");
}

// The e.r.p. comes from datasheet figures and the fix age from the top of the second: neither is
// measured.
TEST_CASE(
    "ADS-L.4.SRD860.APPENDIX: the computed e.r.p. and the fix age bound sit in the nominal range") {
    CHECK(parts::sx1262::kResultingErpCentiDb >= 1200);
    CHECK(parts::sx1262::kResultingErpCentiDb <= 1400);
    CHECK(timing::Transmitter::kFixMaxAgeMs <= 500);
}
