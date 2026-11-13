// Harness, not a test: a simulated device run past its first fixes, stepped, its air tape counted.
#ifndef SKYBLIP_TEST_SUPPORT_SIMULATOR_RUN_H
#define SKYBLIP_TEST_SUPPORT_SIMULATOR_RUN_H

#include <cstdint>
#include <cstring>

#include "core/gnss/first_fix.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "simulator/simulator.h"

namespace skyblip {

// F5: own-ship holds every burst until the receiver's first solutions have
// settled, so a transmit test that starts the clock at zero is a test of the
// settling window and nothing else. This is the far side of it, with the tape
// wiped so the bursts that follow are the ones under test.
inline uint32_t past_settling(simulator::Simulator& simulator) {
    simulator.run(gnss::kFirstFixSettleMs);
    simulator.world().air().clear();
    return gnss::kFirstFixSettleMs;
}

inline void run_on(simulator::Simulator& simulator, uint32_t from_ms, uint32_t for_ms,
                   uint32_t step_ms = simulator::Simulator::kStepMs) {
    for (uint32_t now_ms = from_ms; now_ms <= from_ms + for_ms; now_ms += step_ms)
        simulator.step(now_ms);
}

// Own bursts go on air as chips behind the ADS-L sync word, read back the way a receiver frames
// them.
inline bool packet_of(const simulator::AirRecord& record, protocol::AdslPacket& packet) {
    protocol::Frame frame{};
    if (record.event != simulator::AirEvent::Tx || !simulator::Air::framed(record, frame) ||
        frame.system != protocol::System::AdslDirect)
        return false;
    packet.reset();
    std::memcpy(packet.data, frame.data, protocol::kAdslFrameBytes);
    if (packet.check_crc() != 0) return false;
    packet.descramble();
    return true;
}

inline uint8_t payload_type_of(const simulator::AirRecord& record) {
    protocol::AdslPacket packet{};
    return packet_of(record, packet) ? packet.payload_type : 0;
}

inline int count_payloads(const simulator::Air& air, uint8_t payload_type) {
    int count = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (payload_type_of(air.record_at(i)) == payload_type) count++;
    return count;
}

inline int count_of(const simulator::Air& air, simulator::AirEvent want) {
    int count = 0;
    for (int i = 0; i < air.record_count(); i++)
        if (air.record_at(i).event == want) count++;
    return count;
}

}  // namespace skyblip

#endif
