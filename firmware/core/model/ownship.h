#ifndef SKYBLIP_CORE_MODEL_OWNSHIP_H
#define SKYBLIP_CORE_MODEL_OWNSHIP_H

#include <cstdint>

namespace skyblip::model {
struct Ownship {
    bool fix_valid;
    bool utc_valid;
    bool pps_locked;
    bool climb_valid;
    int32_t latitude_1e7;
    int32_t longitude_1e7;
    // INFO: fc 20nov26 HAE, because ADS-L G.1.7 transmits it and every received altitude uses it
    int32_t altitude_hae_mm;
    // INFO: fc 22nov26 false for a height the receiver did not solve, or dated apart from its
    // position
    bool altitude_hae_valid{true};
    int32_t altitude_msl_mm;
    int32_t pressure_altitude_mm;
    uint32_t pressure_altitude_at_ms;
    bool pressure_altitude_valid;
    int32_t climb_mm_s;
    // INFO: fc 19sep26 what the instruments resolve: the wire's coarser units are the encoder's
    int32_t speed_mm_s;
    // INFO: fc 20nov26 hundredths, 0 to 35999, because the turn rate is differentiated from it
    int32_t track_cdeg;
    // INFO: fc 20nov26 positive to the right, zero when nothing measured it
    int16_t turn_cdps;
    uint32_t utc_s;
    // INFO: fc 18nov26 milliseconds into utc_s: at 2 Hz the quarter the G.1.1 timestamp names
    uint16_t utc_ms;
    // INFO: fc 20nov26 when the solution was solved: G.1.16 holds one over 500 ms old on air
    uint32_t fix_at_ms;
    // INFO: fc 18nov26 fix_at_ms mapped through an anchor a fix set, the dating positions trust
    bool fix_dated;
    // INFO: fc 19nov26 when own-ship took that solution, which decides whether a position redraws
    uint32_t fix_in_hand_ms;
    // INFO: fc 20nov26 zero means the receiver did not report it, not a perfect HDOP
    uint16_t hdop_e2;
    // INFO: fc 22nov26 vertical DOP in hundredths, zero unless the receiver's last GSA set said 3D
    uint16_t vdop_e2;
    // INFO: fc 20nov26 false when altitude_msl_mm used a regional geoid constant
    bool geoid_measured;
    uint16_t prediction_residual_m;
    bool prediction_residual_valid;
    // INFO: fc 20nov26 the fix has held long enough to settle, and nothing transmits before it
    bool tx_settled;
    // INFO: fc 20nov26 true only for the one pass that applied the first fix since boot
    bool fix_acquired;
    uint8_t satellites;
    uint8_t aircraft_category;
    uint8_t flight_state;
};

}  // namespace skyblip::model

#endif
