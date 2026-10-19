#ifndef SKYBLIP_CORE_MODEL_OWNSHIP_H
#define SKYBLIP_CORE_MODEL_OWNSHIP_H

#include <cstdint>

namespace skyblip::model {
struct Ownship {
    bool fix_valid;
    bool utc_valid;
    bool pps_locked;
    // Vertical rate arrives later than the fix (it needs two samples over a
    // window) and can come from a barometer the unit may not have, so it carries
    // its own validity. ADS-L G.1.9 has an explicit "unavailable" code for it.
    bool climb_valid;
    int32_t latitude_1e7;
    int32_t longitude_1e7;
    // Height above the WGS-84 ellipsoid: ADS-L G.1.7 transmits HAE, and every
    // neighbour altitude we receive is measured against the same datum, so a
    // relative height only means something if ours uses it too. altitude_msl_mm is the
    // geoid-referenced figure a panel or an IGC file wants.
    int32_t altitude_hae_mm;
    int32_t altitude_msl_mm;
    int32_t climb_mm_s;
    // INFO: fc 19sep26 what the instruments resolve: the wire's coarser units are the encoder's
    int32_t speed_mm_s;
    // Track over the ground in hundredths of a degree, 0 to 35999. The rate of
    // turn is differentiated from this, so a whole degree here is a tenth of a
    // standard rate turn of error there.
    int32_t track_cdeg;
    // Rate of turn in hundredths of a degree a second, positive to the right.
    // The extrapolation to the transmit instant needs it, so it is own-ship
    // state and not a display value. Zero when nothing measured it.
    int16_t turn_cdps;
    uint32_t utc_s;
    // When the fix that made this state arrived. ADS-L G.1.16 refuses to
    // transmit a navigation solution older than 500 ms.
    uint32_t fix_at_ms;
    // Horizontal dilution of precision in hundredths. Zero means the receiver
    // did not report it, which is not the same as a good one.
    uint16_t hdop_e2;
    // INFO: fc 13sep26 vertical DOP in hundredths, GSA field 17, zero on a 2D solution
    uint16_t vdop_e2;
    // False when the geoid separation behind altitude_m came from a regional constant
    // rather than from the receiver.
    bool geoid_measured;
    // How far the constant-speed, constant-turn, constant-climb model missed the
    // fix that has just arrived, in metres. A model that is wrong is worse than
    // no model, and this is how that is found on the bench.
    uint16_t prediction_residual_m;
    bool prediction_residual_valid;
    // The receiver has held a fix long enough for the solution behind it to have
    // settled. Nothing is transmitted before this is true.
    bool tx_settled;
    // True for the one pass that applied the receiver's first fix since boot.
    // Own-ship is the only writer, so whoever annunciates it reads it here
    // instead of keeping a second watch on the same fact.
    bool fix_acquired;
    uint8_t satellites;
    uint8_t aircraft_category;
    uint8_t flight_state;
};

}  // namespace skyblip::model

#endif
