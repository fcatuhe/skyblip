#ifndef SKYBLIP_CORE_TRAFFIC_CALLSIGNS_H
#define SKYBLIP_CORE_TRAFFIC_CALLSIGNS_H

#include <array>
#include <cstdint>

#include "core/protocol/adsl.h"
#include "core/traffic/table.h"

namespace skyblip::traffic {

// INFO: fc 20sep26 a name outlives the target it belongs to, which ages out in seconds
constexpr uint32_t kCallsignForgetS = 600;

class CallsignTable {
   public:
    static constexpr int kMaxCallsigns = TrafficTable::kMaxTargets;
    static constexpr int kTextBytes = protocol::AdslPacket::kInfoTextBytes + 1;

    void learn(uint8_t address_mapping_table, uint32_t address, const char* callsign,
               uint32_t now_s);
    const char* find(uint8_t address_mapping_table, uint32_t address) const;
    void age_out(uint32_t now_s, uint32_t forget_s = kCallsignForgetS);

    int count() const;
    void clear();

   private:
    struct Entry {
        uint32_t address{0};
        uint32_t heard_s{0};
        char text[kTextBytes]{0};
        uint8_t address_mapping_table{0};
        bool used{false};
    };

    int find_slot(uint8_t address_mapping_table, uint32_t address) const;
    int oldest_slot() const;

    std::array<Entry, kMaxCallsigns> slots_{};
};

}  // namespace skyblip::traffic

#endif
