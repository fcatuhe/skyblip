// A fresh image on probation: it confirms itself once the receiver has spoken, and only then.
#include <cstring>
#include <string>

#include "core/settings/blob.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/boot.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/pages/recovery.h"
#include "runtime/tasks.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

TEST_CASE("product: a fresh image confirms itself once the receiver has spoken, fix or no fix") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);

    // Indoors: radio up, panel drawn, nothing off the UART yet
    rig.run(0, 30000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(rig.state().panel_presented);

    receiver_speaks_without_a_fix(rig);
    rig.run(30000, 30500);
    CHECK(rig.platform.dfu().confirms == 1);
    CHECK(rig.platform.dfu().confirmed());
    CHECK(config(rig).image_state() == dfu::ImageState::Confirmed);

    rig.run(30500, 60000);
    CHECK(rig.platform.dfu().confirms == 1);
}

TEST_CASE("product: a receiver that never speaks keeps the image on probation") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, 120000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);
}

TEST_CASE("product: an image that was installed confirmed is never confirmed again") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 0;
    on_ground(rig, t);
    rig.run(t, t + 5000);
    CHECK(rig.platform.dfu().confirms == 0);
    CHECK(config(rig).image_state() == dfu::ImageState::Confirmed);
}

TEST_CASE("product: a board with no panel does not wait for one to confirm") {
    constexpr ports::Capabilities kNoPanel = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Display));
    Rig rig(kNoPanel);
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    receiver_speaks_without_a_fix(rig);
    rig.run(0, 500);
    CHECK_FALSE(rig.state().panel_presented);
    CHECK(rig.platform.dfu().confirms == 1);
}

TEST_CASE("product: a trailer that will not take the confirmation leaves the image on probation") {
    Rig rig;
    rig.platform.dfu().image_confirmed = false;
    rig.platform.dfu().confirm_fails = true;
    REQUIRE(rig.setup() == Status::Ok);
    receiver_speaks_without_a_fix(rig);
    rig.run(0, 10000);
    CHECK(rig.platform.dfu().confirms == 3);
    CHECK_FALSE(rig.platform.dfu().confirmed());
    CHECK(config(rig).image_state() == dfu::ImageState::Probation);
}
