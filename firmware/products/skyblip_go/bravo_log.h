#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_BRAVO_LOG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_BRAVO_LOG_H

#include <cstdint>

#include "core/comms/peer.h"
#include "core/diagnostics/record.h"

namespace skyblip::go {

enum class BravoVerdict : uint8_t { Folded, Refused, Unplaced };

struct BravoLogEntry {
    BravoVerdict verdict{BravoVerdict::Folded};
    diagnostics::Instant at{};
    uint32_t address{0};
    int32_t pressure_altitude_ft{0};
    bool address_valid{false};
    bool pressure_altitude_valid{false};
    char callsign[comms::kPeerCallsignBytes]{};
};

// INFO: fc 15nov26 a tape as radio::Log is: what scrolls off the bottom is gone
class BravoLog {
   public:
    static constexpr int kMaxEntries = 16;

    void record(const BravoLogEntry& entry) {
        entries_[written_ % kMaxEntries] = entry;
        written_++;
    }

    int count() const { return written_ < kMaxEntries ? static_cast<int>(written_) : kMaxEntries; }
    const BravoLogEntry& newest(int index) const {
        return entries_[(written_ - 1 - static_cast<uint32_t>(index)) % kMaxEntries];
    }

   private:
    BravoLogEntry entries_[kMaxEntries]{};
    uint32_t written_{0};
};

}  // namespace skyblip::go

#endif
