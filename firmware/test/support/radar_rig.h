// Harness, not a test: the radar snapshot of an aircraft in flight, to scale, and the glass it
// draws.
#ifndef SKYBLIP_TEST_SUPPORT_RADAR_RIG_H
#define SKYBLIP_TEST_SUPPORT_RADAR_RIG_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"

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

}  // namespace skyblip::go

#endif
