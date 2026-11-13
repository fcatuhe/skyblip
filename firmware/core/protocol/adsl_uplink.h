#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_UPLINK_H

#include <cstddef>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/util/result.h"
#include "protocols/adsl/uplink.h"

namespace skyblip::protocol {

class AdslUplink {
   public:
    static constexpr int kMaxTargets = adsl::kUplinkMaxTargets;
    static constexpr int kPacketBytes = 1 + adsl::kUplinkMaxLength;

    struct DecodeStatistics {
        int targets{0};
        int corrected{0};
        // Records the frame carried and the decoder refused. Reed-Solomon can
        // correct to a valid codeword that is not the one that was sent, so a
        // frame that passes parity can still hold nonsense: what it holds is
        // checked before any of it becomes traffic.
        int rejected{0};
        // INFO: fc 13nov26 G.3: older than 10 s or dated in the future, so not forwarded as current
        int stale{0};
    };

    // INFO: fc 13nov26 a relayed target is dated now, the instant the ground station forwards it
    Status encode(const model::AircraftObservation* targets, int count, uint32_t utc_s,
                  int32_t into_ms, uint32_t sender_address, uint8_t sender_table,
                  uint8_t out_packet[kPacketBytes]) const;

    Status decode(const uint8_t* packet, size_t bytes, uint32_t utc_s, int32_t into_ms,
                  model::AircraftObservation* targets, int capacity,
                  DecodeStatistics& statistics) const;

   private:
    adsl::Uplink codec_;
};

}

#endif
