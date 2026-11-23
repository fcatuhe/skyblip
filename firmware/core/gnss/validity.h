#ifndef SKYBLIP_CORE_GNSS_VALIDITY_H
#define SKYBLIP_CORE_GNSS_VALIDITY_H

#include <cstdint>

#include "core/gnss/nmea.h"

namespace skyblip::gnss {

// INFO: fc 13sep26 SoftRF's NMEA_EXP_TIME: liveness, not a freshness rule, 7 bursts at 2 Hz
constexpr uint32_t kSentenceMaxAgeMs = 3500;

// INFO: fc 22nov26 the L76K sends GSA once a second at 2 Hz, so an older one missed its turn
constexpr uint32_t kGsaMaxAgeMs = 1500;

// INFO: fc 20nov26 moshe-braner's jump gate (SoftRF.ino:517-523): 16.7 km between two solutions
constexpr int32_t kLatitudeJumpCeiling1e7 = 1500000;
constexpr int32_t kLongitudeJumpCeiling1e7 = 2500000;

enum class FixReject : uint8_t {
    None = 0,
    NoSolution = 1,
    MissingRmc = 2,
    MissingGga = 3,
    Stale = 4,
    NoDate = 5,
    Jump = 6,
};

// INFO: fc 20nov26 GGA field 6: like SoftRF, accept GPS through float RTK, never 6, dead reckoning
constexpr uint8_t kQualityGps = 1;
constexpr uint8_t kQualityFloatRtk = 5;

class FixValidity {
   public:
    void observe(const GnssSolution& solution, Sentence which, uint32_t now_ms);

    FixReject evaluate(uint32_t now_ms) const;
    bool valid(uint32_t now_ms) const { return evaluate(now_ms) == FixReject::None; }

    FixReject check(uint32_t now_ms);

    void expire_fix_mode(GnssSolution& solution, uint32_t now_ms) const;

    FixReject last_reject() const { return last_reject_; }
    uint32_t rejected() const { return rejected_; }

    void reset();

   private:
    uint32_t rmc_heard_ms_{0};
    uint32_t gga_heard_ms_{0};
    uint32_t gsa_heard_ms_{0};
    int32_t previous_latitude_1e7_{0};
    int32_t previous_longitude_1e7_{0};
    uint32_t rejected_{0};
    FixReject last_reject_{FixReject::None};
    bool rmc_heard_{false};
    bool gga_heard_{false};
    bool gsa_heard_{false};
    bool rmc_fix_valid_{false};
    bool gga_fix_valid_{false};
    bool utc_valid_{false};
    bool jumped_{false};
    bool previous_valid_{false};
};

}  // namespace skyblip::gnss

#endif
