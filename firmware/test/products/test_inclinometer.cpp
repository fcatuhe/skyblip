// The ball, end to end: a hub on the bus, a rudder mistake, ink on the glass.
#include <cstdio>
#include <string>

#include "core/util/sha256.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/sixpack.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/update_rig.h"

using namespace skyblip;

namespace {

constexpr int kTurnCoordinatorCx = 34;
constexpr int kTurnCoordinatorCy = 133;
constexpr int kBallY = kTurnCoordinatorCy + 18;
constexpr uint32_t kBootToBall = 4000;

// The chip is a quarter turn from the case and face down with it (imu_mount.h): +X up, +Y right.
void fly_uncoordinated(Rig& rig, int16_t right_mg) {
    rig.platform.chips().imu.set_acceleration(1000, right_mg, 0);
}

bool ink_between(const go::Glass& fb, int from_x, int to_x, int y) {
    for (int x = from_x; x <= to_x; x++)
        if (fb.get_pixel(x, y)) return true;
    return false;
}

}  // namespace

TEST_CASE("inclinometer: a hub that boots gives the six-pack its cage and its ball") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    fly_uncoordinated(rig, 0);
    rig.run(0, kBootToBall);

    CHECK(rig.product.board().imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(rig.state().slip.valid);
    CHECK(rig.state().slip.lateral_mg == 0);
}

TEST_CASE("inclinometer: the ball goes the way the pilot slides, and the rudder follows") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    fly_uncoordinated(rig, -150);
    rig.run(0, kBootToBall);

    CHECK(rig.state().slip.valid);
    CHECK(rig.state().slip.lateral_mg > 0);

    go::SixPackSnapshot snap;
    snap.inclinometer_fitted = true;
    snap.lateral_valid = rig.state().slip.valid;
    snap.lateral_mg = rig.state().slip.lateral_mg;
    go::Glass fb;
    go::draw_sixpack(fb, snap);

    CHECK(ink_between(fb, kTurnCoordinatorCx + 1, kTurnCoordinatorCx + 20, kBallY));
    CHECK_FALSE(ink_between(fb, kTurnCoordinatorCx - 20, kTurnCoordinatorCx - 8, kBallY));
}

TEST_CASE("inclinometer: a hub that stops answering takes the ball off the glass") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    fly_uncoordinated(rig, -150);
    rig.run(0, kBootToBall);
    REQUIRE(rig.state().slip.valid);

    rig.platform.chips().imu.answers = false;
    rig.run(kBootToBall, kBootToBall + flight::kIndicatedStaleMs + 1000);

    CHECK(rig.product.board().imu().stage() == parts::Bhi260::Stage::Failed);
    CHECK_FALSE(rig.state().slip.valid);
}

TEST_CASE("inclinometer: the status page names the stage the hub stopped in") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    fly_uncoordinated(rig, -150);
    rig.run(0, kBootToBall);

    uint32_t t = kBootToBall;
    rig.show(t, go::Page::Status);
    REQUIRE(rig.product.screen().page() == go::Page::Status);
    rig.run(t, t + 1000);
    CHECK(reads_in(rig.product.screen().framebuffer(), "IMU RUN", 0, 55, 200, 70));

    rig.platform.chips().imu.answers = false;
    rig.run(t + 1000, t + 1000 + flight::kIndicatedStaleMs + 1000);
    CHECK(reads_in(rig.product.screen().framebuffer(), "IMU RUN DOWN", 0, 55, 200, 70));
}

// The milliamp the gyroscope costs is never spent: virtual sensor 13 is never subscribed.
TEST_CASE("inclinometer: the hub runs no gyroscope, and the turn rate comes off the fix") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    fly_uncoordinated(rig, 0);
    rig.platform.chips().imu.set_angular_rate(-300, 0, 0);
    rig.run(0, kBootToBall);

    REQUIRE(rig.product.board().imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(rig.platform.chips().imu.gyro_rate_hz == doctest::Approx(0));
    CHECK(rig.state().own.turn_cdps == 0);
}

TEST_CASE("inclinometer: a unit with no hub keeps the dial empty rather than centred") {
    constexpr ports::Capabilities kNoImu = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Inclinometer));
    Rig rig(kNoImu);
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, kBootToBall);

    CHECK_FALSE(ports::has(rig.product.capabilities(), ports::Capability::Inclinometer));
    CHECK_FALSE(rig.state().slip.valid);

    go::SixPackSnapshot snap;
    go::Glass fb;
    go::draw_sixpack(fb, snap);
    CHECK_FALSE(ink_between(fb, kTurnCoordinatorCx - 20, kTurnCoordinatorCx + 20, kBallY));
}

namespace {

constexpr ports::Capabilities kPlainTEcho =
    static_cast<ports::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                     ~static_cast<uint32_t>(ports::Capability::Inclinometer));

const go::BootPart& imu_row(Rig& rig) {
    for (int i = 0; i < go::kBootPartCount; i++)
        if (std::string(rig.product.boot_rows()[i].name) == "IMU")
            return rig.product.boot_rows()[i];
    return rig.product.boot_rows()[0];
}

std::string update_frame(Rig& rig, uint32_t at_ms) {
    rig.send("{\"cmd\":\"update\"}");
    rig.run(at_ms, at_ms + 100);
    return rig.last_on(events::Endpoint::Config);
}

}  // namespace

TEST_CASE("inclinometer: a slim image with no hub image on flash says so on the status page") {
    Rig rig;
    rig.platform.set_imu_firmware_linked(false);
    REQUIRE(rig.setup() == Status::Ok);
    rig.run(0, kBootToBall);

    CHECK(rig.product.board().imu().stage() == parts::Bhi260::Stage::Idle);
    CHECK_FALSE(rig.state().slip.valid);
    uint32_t t = kBootToBall;
    rig.show(t, go::Page::Status);
    rig.run(t, t + 1000);
    CHECK(reads_in(rig.product.screen().framebuffer(), "IMU IDLE NOBLOB", 0, 55, 200, 70));
}

TEST_CASE("inclinometer: the self test names a hub that has no image to boot") {
    Rig rig;
    rig.platform.set_imu_firmware_linked(false);
    REQUIRE(rig.setup() == Status::Ok);
    CHECK(imu_row(rig).state == go::PartState::Pass);
    CHECK(std::string(imu_row(rig).detail) == go::kImuDetailWithoutImage);

    Rig full;
    REQUIRE(full.setup() == Status::Ok);
    CHECK(std::string(imu_row(full).detail) == "BHI260AP");
}

TEST_CASE("inclinometer: the phone is told what the flash holds for the hub") {
    Rig full;
    REQUIRE(full.setup() == Status::Ok);
    full.raise_link();
    full.run(0, 2000);
    const Sha256::Digest& pin = full.platform.imu_firmware_digest();
    char expected[17];
    for (int i = 0; i < 8; i++)
        std::snprintf(expected + 2 * i, 3, "%02x", pin[static_cast<size_t>(i)]);
    CHECK(update_frame(full, 2000).find(std::string("\"imu\":\"") + expected + "\"") !=
          std::string::npos);

    Rig slim;
    slim.platform.set_imu_firmware_linked(false);
    REQUIRE(slim.setup() == Status::Ok);
    slim.raise_link();
    slim.run(0, 200);
    CHECK(update_frame(slim, 200).find("\"imu\":\"missing\"") != std::string::npos);

    Rig plain(kPlainTEcho);
    REQUIRE(plain.setup() == Status::Ok);
    plain.raise_link();
    plain.run(0, 200);
    CHECK(update_frame(plain, 200).find("\"imu\":\"none\"") != std::string::npos);
}

// The bootloader reverts it on the next reset, to the image before, which booted the hub.
TEST_CASE("inclinometer: a slim image that cannot boot its hub never confirms itself") {
    Rig rig;
    rig.platform.set_imu_firmware_linked(false);
    rig.platform.dfu().image_confirmed = false;
    REQUIRE(rig.setup() == Status::Ok);
    receiver_speaks_without_a_fix(rig);
    rig.run(0, 30000);
    CHECK(rig.platform.dfu().confirms == 0);

    Rig plain(kPlainTEcho);
    plain.platform.set_imu_firmware_linked(false);
    plain.platform.dfu().image_confirmed = false;
    REQUIRE(plain.setup() == Status::Ok);
    receiver_speaks_without_a_fix(plain);
    plain.run(0, 5000);
    CHECK(plain.platform.dfu().confirms == 1);
}

TEST_CASE(
    "inclinometer: a full image fills an empty partition, and the slim image after it boots from "
    "it") {
    Rig fitted;
    REQUIRE(fitted.setup() == Status::Ok);
    fitted.run(0, kBootToBall);
    REQUIRE(fitted.product.board().imu_store().holds(fitted.platform.imu_firmware_digest()));

    Rig slim;
    slim.platform.imu_flash().restore(fitted.platform.imu_flash().bytes());
    slim.platform.set_imu_firmware_linked(false);
    REQUIRE(slim.setup() == Status::Ok);
    fly_uncoordinated(slim, 0);
    slim.run(0, kBootToBall);
    CHECK(slim.product.board().imu().stage() == parts::Bhi260::Stage::Running);
    CHECK(slim.state().slip.valid);
}
