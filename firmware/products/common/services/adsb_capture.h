#ifndef SKYBLIP_PRODUCTS_COMMON_SERVICES_ADSB_CAPTURE_H
#define SKYBLIP_PRODUCTS_COMMON_SERVICES_ADSB_CAPTURE_H

#include <array>
#include <cstdint>

#include "core/model/aircraft.h"
#include "protocols/adsb/assembly.h"
#include "runtime/service.h"

namespace skyblip::common {

class AdsbCapture {
   public:
    // INFO: fc 11nov26 a GNSS-baro difference is trusted 10 s, so one record per aircraft in it
    static constexpr uint32_t kRecordPeriodMs = adsb::kHeightDifferenceMaxAgeMs;
    static constexpr int kAircraft = adsb::Assembly::kMaxAircraft;
    // INFO: fc 11nov26 the ring's writer can be blocked about a second, so its budget is a second's
    static constexpr uint32_t kBudgetWindowMs = 1000;
    // INFO: fc 11nov26 a full assembly spread evenly over the period, so no burst outruns the ring
    static constexpr uint32_t kBudgetCeilingRecords =
        static_cast<uint32_t>(kAircraft) * kBudgetWindowMs / kRecordPeriodMs;

    explicit AdsbCapture(runtime::Context& context) : context_(context) {}

    bool record(const model::AircraftObservation& observation, uint32_t now_ms);

    uint32_t refused() const { return refused_; }

   private:
    struct Recorded {
        uint32_t address{0};
        uint32_t at_ms{0};
    };

    runtime::Context& context_;
    std::array<Recorded, kAircraft> recorded_{};
    int held_{0};
    std::array<uint32_t, kBudgetCeilingRecords> spent_at_ms_{};
    uint32_t spent_{0};
    uint32_t oldest_spent_{0};
    uint32_t refused_{0};

    bool affordable(uint32_t now_ms) const;
    void spend(uint32_t now_ms);
    Recorded* find(uint32_t address);
    Recorded* vacancy(uint32_t now_ms);
};

}  // namespace skyblip::common

#endif
