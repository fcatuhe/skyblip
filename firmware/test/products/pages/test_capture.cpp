// The page that states the price of a capture before it takes it, and what it took.
#include "doctest/doctest.h"
#include "products/skyblip_go/pages/capture.h"
#include "test/support/glass_text.h"

using namespace skyblip::go;
using skyblip::reads_in;
using skyblip::diagnostics::Profile;

namespace {

// 266 sectors of 170 slots is 45,220: 4110 s at Full's 39,600 an hour, 678,300 s at a run's 240.
constexpr uint32_t kFullKeepsS = 4110;
constexpr uint32_t kPowerRunKeepsS = 678300;

CaptureSnapshot disarmed_device() {
    CaptureSnapshot snapshot;
    snapshot.uptime_s = 812;
    snapshot.capture.available = true;
    snapshot.capture.pool_sectors = 330;
    snapshot.capture.growth_sectors = 266;
    snapshot.capture.evicted_flights = 3;
    snapshot.focus_keeps_s = kFullKeepsS;
    return snapshot;
}

CaptureSnapshot offering(Profile focus) {
    CaptureSnapshot snapshot = disarmed_device();
    snapshot.focus = focus;
    snapshot.focus_keeps_s = focus == Profile::Full ? kFullKeepsS : kPowerRunKeepsS;
    return snapshot;
}

CaptureSnapshot armed_device() {
    CaptureSnapshot snapshot = disarmed_device();
    snapshot.capture.armed = true;
    snapshot.running = Profile::Full;
    snapshot.capture.session_id = 1785628800;
    snapshot.capture.records = 4213;
    snapshot.capture.dropped = 17;
    snapshot.capture.sectors = 25;
    snapshot.capture.keeps_s = 3600;
    return snapshot;
}

bool focused(const Glass& canvas, const char* name) {
    return reads_in(canvas, name, 0, 28, 200, 74, 1, /*ink=*/false);
}

}  // namespace

TEST_CASE("capture page: a disarmed device states the price before it is paid") {
    Glass canvas;
    draw_capture(canvas, disarmed_device());

    CHECK(reads_in(canvas, "CAPTURE", 0, 0, 200, 14));
    CHECK(reads_in(canvas, "T+812", 100, 0, 200, 14));
    CHECK(reads_in(canvas, "STATE OFF", 0, 14, 200, 40));
    CHECK(reads_in(canvas, "POWER RUN", 0, 42, 200, 70));
    CHECK(reads_in(canvas, "FLIGHT RUN", 0, 56, 200, 84));
    CHECK(reads_in(canvas, "TAKES 266 OF 330 SECTORS", 0, 70, 200, 98));
    CHECK(reads_in(canvas, "EVICTS 3 FLIGHTS", 0, 84, 200, 112));
    CHECK(reads_in(canvas, "KEEPS 1H08 ROLLING", 0, 98, 200, 126));
    CHECK(reads_in(canvas, "PAD PICKS", 0, 170, 200, 200));
    CHECK(reads_in(canvas, "PRESS TWICE TO ARM", 0, 170, 200, 200));
}

TEST_CASE("capture page: the full capture and the two runs quote spans two orders apart") {
    Glass full;
    draw_capture(full, offering(Profile::Full));
    Glass power_run;
    draw_capture(power_run, offering(Profile::PowerRun));
    Glass flight_run;
    draw_capture(flight_run, offering(Profile::FlightRun));

    CHECK(reads_in(full, "KEEPS 1H08 ROLLING", 0, 98, 200, 126));
    CHECK(reads_in(power_run, "KEEPS 188H25 ROLLING", 0, 98, 200, 126));
    CHECK_FALSE(reads_in(power_run, "KEEPS 1H08 ROLLING", 0, 0, 200, 200));
    CHECK(reads_in(flight_run, "KEEPS 188H25 ROLLING", 0, 98, 200, 126));
}

TEST_CASE("capture page: the flight run records power and duty, as the power run does") {
    Glass canvas;
    draw_capture(canvas, offering(Profile::FlightRun));
    CHECK(focused(canvas, "FLIGHT RUN"));
    CHECK(reads_in(canvas, "POWER AND DUTY", 100, 56, 200, 84, 1, /*ink=*/false));
    CHECK(reads_in(canvas, "POWER AND DUTY", 100, 42, 200, 70));
    CHECK(reads_in(canvas, "EVERY SUBJECT", 100, 28, 200, 56));
}

TEST_CASE("capture page: the capture in focus is the one under the bar") {
    Glass full;
    draw_capture(full, offering(Profile::Full));
    CHECK(focused(full, "FULL"));
    CHECK(reads_in(full, "POWER RUN", 0, 42, 200, 70));
    CHECK_FALSE(focused(full, "POWER RUN"));

    Glass power_run;
    draw_capture(power_run, offering(Profile::PowerRun));
    CHECK(focused(power_run, "POWER RUN"));
    CHECK(reads_in(power_run, "FULL", 0, 28, 200, 56));
    CHECK_FALSE(focused(power_run, "FULL"));
    CHECK_FALSE(focused(power_run, "FLIGHT RUN"));
}

TEST_CASE("capture page: an armed device shows what it wrote, what it lost and what it holds") {
    Glass canvas;
    draw_capture(canvas, armed_device());

    CHECK(reads_in(canvas, "STATE ARMED FULL", 0, 14, 200, 40));
    CHECK(reads_in(canvas, "SESSION 1785628800", 0, 28, 200, 56));
    CHECK(reads_in(canvas, "WROTE 4213 DROP 17", 0, 42, 200, 70));
    CHECK(reads_in(canvas, "SECTORS 25 KEEPS 1H00", 0, 56, 200, 84));
    CHECK(reads_in(canvas, "PRESS TWICE TO STOP", 0, 170, 200, 200));
}

TEST_CASE("capture page: an armed device names the capture that is running") {
    CaptureSnapshot snapshot = armed_device();
    snapshot.running = Profile::PowerRun;
    Glass canvas;
    draw_capture(canvas, snapshot);

    CHECK(reads_in(canvas, "STATE ARMED POWER RUN", 0, 14, 200, 40));
    CHECK_FALSE(reads_in(canvas, "STATE ARMED FULL", 0, 0, 200, 200));
    CHECK_FALSE(reads_in(canvas, "PAD PICKS", 0, 0, 200, 200));

    snapshot.running = Profile::FlightRun;
    Glass flight_run;
    draw_capture(flight_run, snapshot);
    CHECK(reads_in(flight_run, "STATE ARMED FLIGHT RUN", 0, 14, 200, 40));
}

// A capture that stopped on its own says so: the pilot did not ask for it.
TEST_CASE("capture page: a capture the allocator refused says it stopped and why") {
    CaptureSnapshot snapshot = disarmed_device();
    snapshot.capture.stopped = skyblip::bus::CaptureStop::NoSectors;
    snapshot.capture.records = 45220;
    Glass canvas;
    draw_capture(canvas, snapshot);

    CHECK(reads_in(canvas, "STATE STOPPED NO SECTORS", 0, 14, 200, 40));
    CHECK(reads_in(canvas, "WROTE 45220 DROP 0", 0, 112, 200, 140));
}

// A storage fault the pilot cannot see is a corpus with holes nobody accounted for.
TEST_CASE("capture page: a refused flash write is on the glass, not only in a counter") {
    CaptureSnapshot snapshot = armed_device();
    snapshot.capture.faults = 3;
    snapshot.capture.unreadable_sectors = 1;
    Glass canvas;
    draw_capture(canvas, snapshot);

    CHECK(reads_in(canvas, "FLASH FAULTS 3 UNREAD 1", 0, 84, 200, 126));
}

TEST_CASE("capture page: a device with no partition offers no gesture at all") {
    CaptureSnapshot snapshot;
    snapshot.capture.available = false;
    Glass canvas;
    draw_capture(canvas, snapshot);

    CHECK(reads_in(canvas, "NO FLASH FITTED", 0, 170, 200, 200));
    CHECK_FALSE(reads_in(canvas, "PRESS TWICE TO ARM", 0, 0, 200, 200));
    CHECK_FALSE(reads_in(canvas, "POWER RUN", 0, 0, 200, 200));
    CHECK_FALSE(reads_in(canvas, "FLIGHT RUN", 0, 0, 200, 200));
}
