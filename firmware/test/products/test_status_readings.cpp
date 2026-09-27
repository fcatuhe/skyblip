// The status page is the bench's: it prints what the sensors read, never what the glass is shown.
#include "core/flight/atmosphere.h"
#include "test/support/glass_text.h"
#include "test/support/screen_rig.h"

using skyblip::reads_in;

namespace {

constexpr int kImuRowTop = 55, kImuRowBottom = 70;
constexpr int kClimbRowTop = 149, kClimbRowBottom = 169;
constexpr int kStdRowTop = 117, kStdRowBottom = 137;
constexpr int kBatteryRowTop = 165, kBatteryRowBottom = 185;

void show_status(Rig& rig, uint32_t& t) {
    rig.run_seconds(t, 3);
    rig.show(t, go::Page::Status);
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.page() == go::Page::Status);
}

}  // namespace

TEST_CASE("status readings: the climb is the measurement, not the damped rate the six-pack shows") {
    Rig rig;
    rig.state.own.climb_valid = true;
    rig.state.own.climb_mm_s = -1234;
    rig.state.indicated.climb_mm_s = -500;
    uint32_t t = 0;
    show_status(rig, t);

    const go::Glass& fb = rig.screen.framebuffer();
    CHECK(reads_in(fb, "-1.234", 0, kClimbRowTop, 200, kClimbRowBottom));
    CHECK_FALSE(reads_in(fb, "-0.500", 0, kClimbRowTop, 200, kClimbRowBottom));
}

TEST_CASE("status readings: the ball is the last sample, not the damped ball the dial hangs") {
    Rig rig;
    rig.state.imu.stage = "RUN";
    rig.state.slip.valid = true;
    rig.state.slip.lateral_mg = -40;
    rig.state.slip.measured_mg = -120;
    uint32_t t = 0;
    show_status(rig, t);

    CHECK(reads_in(rig.screen.framebuffer(), "IMU RUN -120mg", 0, kImuRowTop, 200, kImuRowBottom));
}

TEST_CASE("status readings: the cell is the last sample, not the median the gauge keeps") {
    Rig rig;
    rig.state.power.battery.valid = true;
    rig.state.power.battery.millivolts = 3950;
    rig.state.power.battery.sample_mv = 3871;
    uint32_t t = 0;
    show_status(rig, t);

    const go::Glass& fb = rig.screen.framebuffer();
    CHECK(reads_in(fb, "3.871", 0, kBatteryRowTop, 200, kBatteryRowBottom));
    CHECK_FALSE(reads_in(fb, "3.950", 0, kBatteryRowTop, 200, kBatteryRowBottom));
}

TEST_CASE("status readings: pressure altitude is taken from the millipascal, not the pascal") {
    Rig rig;
    rig.state.baro.active = true;
    rig.state.baro.pressure_mpa = flight::alt_mm_to_pressure_mpa(1500123);
    uint32_t t = 0;
    show_status(rig, t);

    CHECK(reads_in(rig.screen.framebuffer(), "1500.12", 0, kStdRowTop, 200, kStdRowBottom));
}
