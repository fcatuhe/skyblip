#ifndef SKYBLIP_CORE_FLIGHT_INDICATED_H
#define SKYBLIP_CORE_FLIGHT_INDICATED_H

#include <cstdint>

namespace skyblip::flight {

constexpr uint32_t kIndicatedRateDampingMs = 2000;

class IndicatedRate {
   public:
    void observe(int32_t measured, uint32_t now_ms);
    void reset() { seen_ = false; }
    int32_t value() const;

   private:
    static constexpr int64_t kScale = 256;
    int64_t damped_acc_{0};
    uint32_t last_ms_{0};
    bool seen_{false};
};

}  // namespace skyblip::flight

#endif
