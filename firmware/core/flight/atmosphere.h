#ifndef SKYBLIP_CORE_FLIGHT_ATMOSPHERE_H
#define SKYBLIP_CORE_FLIGHT_ATMOSPHERE_H

#include <cstdint>

namespace skyblip::flight {

constexpr uint32_t kIsaSeaLevelPa = 101325;
constexpr int16_t kIsaSeaLevelDeciCelsius = 150;

int32_t pressure_to_altitude_mm(uint32_t pressure_mpa);
int32_t pressure_to_altitude_cm(uint32_t pressure_pa);

// INFO: fc 20nov26 bisects the forward table, so a modelled barometer cannot drift from the curve
uint32_t altitude_mm_to_pressure_mpa(int32_t altitude_mm);

// INFO: fc 20nov26 shorter and the sensor noise is the rate, longer and the rate is history
constexpr uint32_t kClimbWindowFloorMs = 500;
constexpr uint32_t kClimbWindowCeilingMs = 10000;

bool climb_mm_s_from_altitude(int32_t altitude_mm_now, int32_t altitude_mm_then,
                              uint32_t elapsed_ms, int32_t& out_mm_s);

// INFO: fc 22nov26 every height gives a climb over the window behind it, so the newest dates it
class ClimbWindow {
   public:
    explicit constexpr ClimbWindow(uint32_t window_ms) : window_ms_(window_ms) {}

    bool observe(int32_t altitude_mm, uint32_t at_ms, int32_t& out_mm_s);
    void reset() { count_ = 0; }

   private:
    struct Height {
        int32_t altitude_mm{0};
        uint32_t at_ms{0};
    };
    static constexpr int kHeights = 8;
    // INFO: fc 22nov26 a pass moves a quarter's instant, so a height this short of a window back
    // counts
    static constexpr uint32_t kClimbWindowSlackMs = 125;

    Height heights_[kHeights]{};
    int newest_{0};
    int count_{0};
    uint32_t window_ms_;
};

}  // namespace skyblip::flight

#endif
