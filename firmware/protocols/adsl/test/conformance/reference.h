// Harness, not a test: the Traffic payload the clause cases read, built by the component alone.
#ifndef SKYBLIP_PROTOCOLS_ADSL_TEST_CONFORMANCE_REFERENCE_H
#define SKYBLIP_PROTOCOLS_ADSL_TEST_CONFORMANCE_REFERENCE_H

#include <cstddef>
#include <cstdint>

#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/common/manchester.h"

namespace skyblip::adsl::conformance {

constexpr size_t kTxChipBytes = 2u * (wire::kSyncWordBytes + kFrameBytes);

inline Traffic reference_traffic() {
    Traffic traffic;
    traffic.address = 0x123456;
    traffic.address_mapping_table = 6;
    traffic.flight_state = 2;
    traffic.aircraft_category = 4;
    traffic.emergency = 1;
    traffic.position_valid = true;
    traffic.latitude_1e7 = 481234500;
    traffic.longitude_1e7 = 81234500;
    traffic.altitude_valid = true;
    traffic.altitude_m = 1234;
    traffic.speed_valid = true;
    traffic.speed_q = 180;
    traffic.climb_valid = true;
    traffic.climb_e8 = -44;
    traffic.track_c9 = 256;
    traffic.integrity.horizontal_accuracy = 5;
    traffic.integrity.vertical_accuracy = 2;
    traffic.integrity.velocity_accuracy = 1;
    return traffic;
}

inline Packet traffic_packet(uint32_t address = 0x123456, uint8_t table = 6) {
    Traffic traffic = reference_traffic();
    traffic.address = address;
    traffic.address_mapping_table = table;
    Packet packet{};
    encode_traffic(packet, traffic);
    return packet;
}

inline bool heard(const Packet& packet, Traffic& out) {
    return decode_traffic(packet, out) && out.position_valid;
}

inline uint16_t chips_of(uint8_t byte) {
    uint8_t coded[2] = {0, 0};
    wire::manchester_encode(&byte, 1, coded);
    return static_cast<uint16_t>((coded[0] << 8) | coded[1]);
}

}

#endif
