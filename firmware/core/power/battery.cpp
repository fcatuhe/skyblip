#include "core/power/battery.h"

#include <algorithm>

#include "core/events/sensor.h"

namespace skyblip::power {

namespace {

struct Point {
    uint16_t millivolts;
    uint8_t percent;
};

// INFO: fc 09mar26 both curves are textbook, not this pack on this board | 20sep26 README.md
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

constexpr Point kChargeCurve[] = {
    {3400, 0},  {3600, 5},  {3700, 12}, {3800, 25}, {3900, 40}, {4000, 55},
    {4050, 65}, {4100, 75}, {4150, 82}, {4180, 90}, {4190, 95}, {4200, 100},
};

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

uint8_t percent_from_mv(uint16_t millivolts, bool charging) {
    return charging ? percent_on(kChargeCurve, millivolts)
                    : percent_on(kDischargeCurve, millivolts);
}

void Gauge::apply(const events::BatterySample& sample) {
    for (int i = kWindowSamples - 1; i > 0; i--) recent_[i] = recent_[i - 1];
    recent_[0] = sample.millivolts;
    if (seen_ < kWindowSamples) seen_++;

    // Rejecting a transient takes three readings. Before that the newest is
    // everything the gauge knows.
    const uint16_t millivolts =
        seen_ < kWindowSamples ? recent_[0] : median_of(recent_[0], recent_[1], recent_[2]);
    const bool was_charging = state_.charging;
    const bool charging = sample.external_power && millivolts < kChargeCompleteMv;
    const uint8_t percent = percent_from_mv(millivolts, charging);

    // Unplugging swaps the curve under the reading, and the cell relaxes upwards
    // once the charge current stops: both are direction changes, so the gauge
    // re-seats on the new curve instead of holding the old number.
    const bool reseat = !state_.valid || charging != was_charging;
    const bool topped_off = sample.external_power && !charging;
    if (reseat || topped_off)
        state_.percent = percent;
    else if (charging)
        state_.percent = std::max(percent, state_.percent);
    else
        state_.percent = std::min(percent, state_.percent);

    state_.millivolts = millivolts;
    state_.sample_mv = sample.millivolts;
    state_.external_power = sample.external_power;
    state_.charging = charging;
    state_.valid = true;
}

}  // namespace skyblip::power
