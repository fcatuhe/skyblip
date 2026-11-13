// ADS-L 4 SRD-860 issue 2 Subpart B, the overview: bit order, transmission sequence and versioning.
#include "doctest/doctest.h"

// TODO: fc 18sep26 soft versioning needs G.2's Max Protocol Version, neither sent nor tracked
TEST_CASE("ADS-L.4.SRD860.B.5: the version transmitted is one every close neighbour can read" *
          doctest::skip()) {
    FAIL(
        "no peer's Max Protocol Version is tracked, so nothing lowers the version for a neighbour");
}
