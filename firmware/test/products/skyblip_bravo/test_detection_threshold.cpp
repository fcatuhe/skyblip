// The 1090 MHz detection threshold holds the false-alarm rate in a band, as radar CFAR does.
#include <array>

#include "doctest/doctest.h"
#include "products/skyblip_bravo/detection_threshold.h"

using namespace skyblip;
using bravo::DetectionThreshold;

namespace {

// Impossible-format frames that estimate the given false-alarm rate over one window.
uint32_t impossible_for(uint32_t false_alarm_hz) {
    return false_alarm_hz * DetectionThreshold::kFalseAlarmWindowMs / 1000 *
           static_cast<uint32_t>(DetectionThreshold::kImpossibleFormats.size()) /
           DetectionThreshold::kFormatCodes;
}

struct Windows {
    DetectionThreshold threshold;
    uint32_t impossible{0};
    uint32_t now_ms{0};

    Windows() { threshold.update(impossible, now_ms); }

    bool next(uint32_t false_alarm_hz) {
        impossible += impossible_for(false_alarm_hz);
        now_ms += DetectionThreshold::kFalseAlarmWindowMs;
        return threshold.update(impossible, now_ms);
    }
};

}  // namespace

TEST_CASE("detection threshold: noise inside the band holds the floor where weak aircraft decode") {
    Windows windows;
    for (int i = 0; i < 10; i++) CHECK_FALSE(windows.next(60));
    CHECK(windows.threshold.threshold_dbm() == DetectionThreshold::kFloorDbm);
    CHECK(windows.threshold.false_alarm_hz() >= 55);
    CHECK(windows.threshold.false_alarm_hz() <= 60);
}

TEST_CASE("detection threshold: a flood raises it fast, noise over the band a dB a window" *
          doctest::test_suite("main")) {
    Windows windows;
    REQUIRE(windows.next(1500));
    CHECK(windows.threshold.threshold_dbm() ==
          DetectionThreshold::kFloorDbm + DetectionThreshold::kFloodStepDb);
    REQUIRE(windows.next(DetectionThreshold::kFalseAlarmCeilingHz + 40));
    CHECK(windows.threshold.threshold_dbm() == DetectionThreshold::kFloorDbm +
                                                   DetectionThreshold::kFloodStepDb +
                                                   DetectionThreshold::kStepDb);
    CHECK_FALSE(windows.next(DetectionThreshold::kFalseAlarmCeilingHz - 10));
}

TEST_CASE("detection threshold: a quiet band lowers it a dB a window, back to the floor") {
    Windows windows;
    for (int i = 0; i < 3; i++) windows.next(2000);
    const int16_t raised = windows.threshold.threshold_dbm();
    REQUIRE(raised > DetectionThreshold::kFloorDbm);
    REQUIRE(windows.next(5));
    CHECK(windows.threshold.threshold_dbm() == raised - DetectionThreshold::kStepDb);
    for (int i = 0; i < 20; i++) windows.next(5);
    CHECK(windows.threshold.threshold_dbm() == DetectionThreshold::kFloorDbm);
}

TEST_CASE("detection threshold: no flood carries it past the ceiling the soak found deaf") {
    Windows windows;
    for (int i = 0; i < 20; i++) windows.next(100000);
    CHECK(windows.threshold.threshold_dbm() == DetectionThreshold::kCeilingDbm);
}

TEST_CASE("detection threshold: nothing changes inside a window, whatever arrives in it") {
    Windows windows;
    windows.impossible += impossible_for(100000);
    CHECK_FALSE(
        windows.threshold.update(windows.impossible, DetectionThreshold::kFalseAlarmWindowMs - 1));
    CHECK(windows.threshold.threshold_dbm() == DetectionThreshold::kFloorDbm);
}

TEST_CASE("detection threshold: only formats no transponder sends are counted as noise") {
    std::array<uint32_t, adsb::kDownlinkFormats> formats{};
    formats[17] = 900;
    formats[11] = 400;
    formats[4] = 50;
    formats[24] = 70;
    formats[1] = 3;
    formats[15] = 4;
    CHECK(DetectionThreshold::impossible_of(formats) == 7);
}
