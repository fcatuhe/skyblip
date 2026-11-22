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

// INFO: fc 22nov26 G.1 offsets 64/72/86 hold FF/3FFF/1FF, packed with Python from E.2 and E.3.1
inline constexpr uint8_t kIssue1EndpointFrame[24] = {
    0x00, 0x58, 0x03, 0x5D, 0xDD, 0x45, 0xA0, 0xAE, 0x9C, 0x66, 0x97, 0x94,
    0xFD, 0xDF, 0xF4, 0x5C, 0x22, 0x35, 0x15, 0x8E, 0x37, 0x50, 0xC3, 0xC2,
};
inline constexpr uint8_t kIssue2EndpointFrame[24] = {
    0x01, 0x58, 0x03, 0x5D, 0xDD, 0x45, 0xA0, 0xAE, 0x9C, 0x66, 0x97, 0x94,
    0xFD, 0xDF, 0xF4, 0x5C, 0x22, 0x35, 0x15, 0x8E, 0x37, 0x33, 0xCB, 0x15,
};

inline Traffic reference_traffic() {
    Traffic traffic;
    traffic.address = 0x123456;
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
    Packet packet{};
    encode_traffic(packet, table, traffic);
    return packet;
}

inline bool heard(const Packet& packet, Traffic& out) {
    return decode_traffic(packet, out) == wire::Status::Ok && out.position_valid;
}

inline uint16_t chips_of(uint8_t byte) {
    uint8_t coded[2] = {0, 0};
    wire::manchester_encode(&byte, 1, coded);
    return static_cast<uint16_t>((coded[0] << 8) | coded[1]);
}

}

#endif
