// The Bravo pin a paired Go keeps in its settings blob, version 13, and what it refuses.
#include <cstring>
#include <string>

#include "core/events/central.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"

using namespace skyblip;
using namespace skyblip::go;

TEST_CASE("settings: a Bravo pin whose address type no controller names is refused") {
    Settings settings = defaults();
    settings.bravo.type = kBluetoothAddressTypeCeiling;
    CHECK(validate(settings) == Status::Ok);
    settings.bravo.type = kBluetoothAddressTypeCeiling + 1;
    CHECK(validate(settings) == Status::OutOfRange);
}
