// Harness, not a test: the radar snapshot of an aircraft in flight, and the glass it draws.
#ifndef SKYBLIP_TEST_SUPPORT_RADAR_RIG_H
#define SKYBLIP_TEST_SUPPORT_RADAR_RIG_H

#include <cstdint>

#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/radar.h"

namespace skyblip::go {

inline RadarSnapshot flying(uint16_t track_deg) {
    RadarSnapshot snap;
    snap.fix_valid = true;
    snap.range_step = kDefaultRangeStep;
    snap.track_cdeg = track_deg * 100;
    snap.flight_time_valid = true;
    snap.in_flight = true;
    snap.flight_seconds = 42 * 60;
    snap.receiver_listening = true;
    return snap;
}

inline Glass radar(const RadarSnapshot& snap) {
    Glass fb;
    draw_radar(fb, snap);
    return fb;
}

}  // namespace skyblip::go

#endif
