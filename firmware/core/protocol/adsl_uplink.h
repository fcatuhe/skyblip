#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H

#include <cstdint>

#include "core/model/aircraft.h"
#include "core/util/result.h"
#include "protocols/adsl/uplink.h"

namespace skyblip::protocol {

class AdslUplink {
   public:
    static constexpr int kMaxTargets = adsl::Uplink::kMaxTargets;
    static constexpr int kFrameBytes = adsl::Uplink::kFrameBytes;
    using DecodeStatistics = adsl::Uplink::DecodeStatistics;

    Status encode(const model::AircraftObservation* targets, int count, uint8_t key_index,
                  uint8_t out_frame[kFrameBytes]) const;

    Status decode(const uint8_t frame[kFrameBytes], model::AircraftObservation* targets,
                  int capacity, DecodeStatistics& statistics) const;

   private:
    adsl::Uplink codec_;
};

}

#endif
