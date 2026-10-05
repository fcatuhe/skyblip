#include "core/power/battery.h"

#include <algorithm>

#include "core/events/sensor.h"

namespace skyblip::power {

namespace {

struct Point {
    uint16_t millivolts;
    uint8_t percent;
};

// INFO: fc 09mar26 textbook, not this pack on this board | 20sep26 README.md
constexpr Point kDischargeCurve[] = {
    {3200, 0},  {3300, 2},  {3500, 5},  {3600, 12}, {3700, 25}, {3750, 40},  {3800, 55},
    {3850, 65}, {3900, 75}, {3950, 83}, {4000, 89}, {4100, 95}, {4200, 100},
};

static_assert(kDischargeCurve[0].millivolts == kEmptyMv,
              "the gauge reads zero where the device stops, or a pilot flies on a percentage "
              "that ran out before the cell did");
static_assert(kDischargeCurve[0].percent == 0 &&
                  kDischargeCurve[sizeof(kDischargeCurve) / sizeof(Point) - 1].millivolts ==
                      kFullMv,
              "a curve that does not span empty to full is read past its ends");

uint16_t median_of(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
        const uint16_t swap = a;
        a = b;
        b = swap;
    }
    if (b > c) b = c > a ? c : a;
    return b;
}

template <int N>
uint8_t percent_on(const Point (&curve)[N], uint16_t millivolts) {
    if (millivolts <= curve[0].millivolts) return curve[0].percent;
    for (int i = 1; i < N; i++) {
        if (millivolts > curve[i].millivolts) continue;
        const Point& low = curve[i - 1];
        const Point& high = curve[i];
        const int32_t span_mv = high.millivolts - low.millivolts;
        const int32_t span_percent = high.percent - low.percent;
        const int32_t into = millivolts - low.millivolts;
        return static_cast<uint8_t>(low.percent + (into * span_percent + span_mv / 2) / span_mv);
    }
    return curve[N - 1].percent;
}

}  // namespace

uint16_t calibrated_mv(uint16_t raw_mv, int16_t offset_mv) {
    const int32_t trimmed = static_cast<int32_t>(raw_mv) + offset_mv;
    if (trimmed < 0) return 0;
    if (trimmed > 0xFFFF) return 0xFFFF;
    return static_cast<uint16_t>(trimmed);
}

events::BatterySample calibrated(const events::BatterySample& raw, int16_t offset_mv) {
    events::BatterySample out = raw;
    out.millivolts = calibrated_mv(raw.millivolts, offset_mv);
    return out;
}

uint8_t percent_from_mv(uint16_t millivolts) { return percent_on(kDischargeCurve, millivolts); }

void Gauge::apply(const events::BatterySample& sample) {
    state_.sample_mv = sample.millivolts;
    state_.external_power = sample.external_power;
    state_.charging = sample.external_power;

    const bool plausible = plausible_mv(sample.millivolts);
    if (!plausible) refused_++;
    if (!plausible || sample.external_power) {
        seen_ = 0;
        state_.millivolts = sample.millivolts;
        state_.percent = 0;
        state_.valid = false;
        return;
    }

    for (int i = kWindowSamples - 1; i > 0; i--) recent_[i] = recent_[i - 1];
    recent_[0] = sample.millivolts;
    if (seen_ < kWindowSamples) seen_++;

    // Rejecting a transient takes three readings. Before that the newest is
    // everything the gauge knows.
    const uint16_t millivolts =
        seen_ < kWindowSamples ? recent_[0] : median_of(recent_[0], recent_[1], recent_[2]);
    const uint8_t percent = percent_from_mv(millivolts);
    state_.percent = state_.valid ? std::min(percent, state_.percent) : percent;
    state_.millivolts = millivolts;
    state_.valid = true;
}

}  // namespace skyblip::power
