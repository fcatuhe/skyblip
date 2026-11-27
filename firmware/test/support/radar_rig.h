// Harness, not a test: the radar snapshot of an aircraft in flight, to scale, and the glass it
// draws.
#ifndef SKYBLIP_TEST_SUPPORT_RADAR_RIG_H
#define SKYBLIP_TEST_SUPPORT_RADAR_RIG_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"
#include "test/support/glass_text.h"

namespace skyblip::go {

inline RadarSnapshot flying(uint16_t track_deg) {
    RadarSnapshot snapshot;
    snapshot.fix_valid = true;
    snapshot.plot = RadarPlot::ToScale;
    snapshot.range_step = kDefaultRangeStep;
    snapshot.track_cdeg = track_deg * 100;
    snapshot.flight_time_valid = true;
    snapshot.flight_running = true;
    snapshot.flight_seconds = 42 * 60;
    snapshot.receiver_listening = true;
    return snapshot;
}

inline Glass radar(const RadarSnapshot& snapshot) {
    Glass canvas;
    draw_radar(canvas, snapshot);
    return canvas;
}

// The top-right corner, where the radar says what is wrong with a Bravo: black, or reversed.
inline bool corner_reads(const ui::Canvas& canvas, const char* text, bool ink = true) {
    return reads_in(canvas, text, 140, 0, kGlassWidth, 34, 1, ink);
}

// The footer's right end, where a live Bravo's B stands by the traffic count it adds to.
inline bool marks_bravo_live(const ui::Canvas& canvas) {
    return reads_in(canvas, "B", 140, 150, kGlassWidth, kGlassHeight);
}

}  // namespace skyblip::go

#endif
