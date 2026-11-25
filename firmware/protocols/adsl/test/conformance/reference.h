// Harness, not a test: the reference Traffic, and the clauses' sample code laying out an uplink.
#ifndef SKYBLIP_PROTOCOLS_ADSL_TEST_CONFORMANCE_REFERENCE_H
#define SKYBLIP_PROTOCOLS_ADSL_TEST_CONFORMANCE_REFERENCE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

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

// E.2's own sample implementation, transcribed, with the zero key the clause names for scrambling.
inline void spec_xxtea(uint32_t* data, uint32_t words, const uint32_t* key, uint32_t rounds) {
    const uint32_t kDelta = 0x9E3779B9u;
    uint32_t next = data[0];
    uint32_t sum = 0;
    uint32_t previous = data[words - 1];
    uint32_t rounds_left = rounds;
    while (rounds_left-- > 0) {
        sum += kDelta;
        const uint32_t key_offset = (sum >> 2) & 3u;
        uint32_t index = 0;
        for (index = 0; index < words - 1; index++) {
            next = data[index + 1];
            data[index] += (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
                           ((sum ^ next) + (key[(index & 3u) ^ key_offset] ^ previous));
            previous = data[index];
        }
        next = data[0];
        data[words - 1] += (((previous >> 5) ^ (next << 2)) + ((next >> 3) ^ (previous << 4))) ^
                           ((sum ^ next) + (key[(index & 3u) ^ key_offset] ^ previous));
        previous = data[words - 1];
    }
}

// E.3.1's own sample implementation, transcribed.
inline uint32_t spec_crc24(const uint8_t* data, size_t bytes) {
    const uint32_t kPolynomial = 0xFFFA0480u;
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes + 3; i++) {
        crc |= i < bytes ? data[i] : 0u;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80000000u) crc ^= kPolynomial;
            crc <<= 1;
        }
    }
    return crc >> 8;
}

// INFO: fc 13nov26 E.3.2 transcribed, reading element 11 as alpha^11 the way CCSDS 131.0 does
inline uint8_t spec_multiply(uint8_t left, uint8_t right) {
    uint8_t product = 0;
    while (right != 0) {
        if (right & 1u) product ^= left;
        const bool carry = (left & 0x80u) != 0;
        left = static_cast<uint8_t>(left << 1);
        if (carry) left ^= 0x1Du;
        right = static_cast<uint8_t>(right >> 1);
    }
    return product;
}

inline uint8_t spec_power(uint8_t base, int exponent) {
    uint8_t result = 1;
    for (int i = 0; i < exponent; i++) result = spec_multiply(result, base);
    return result;
}

inline void spec_reed_solomon(const uint8_t* data, uint8_t* parity) {
    const int kParity = 32;
    uint8_t generator[kParity + 1] = {1};
    for (int i = 0; i < kParity; i++) {
        const uint8_t root = spec_power(spec_power(2, 11), 121 + i);
        for (int j = i + 1; j > 0; j--)
            generator[j] =
                static_cast<uint8_t>(generator[j - 1] ^ spec_multiply(generator[j], root));
        generator[0] = spec_multiply(generator[0], root);
    }
    uint8_t remainder[kParity] = {0};
    for (int i = 0; i < 223; i++) {
        const uint8_t feedback = data[i] ^ remainder[0];
        for (int j = 0; j < kParity - 1; j++)
            remainder[j] = static_cast<uint8_t>(
                remainder[j + 1] ^ spec_multiply(feedback, generator[kParity - 1 - j]));
        remainder[kParity - 1] = spec_multiply(feedback, generator[0]);
    }
    std::memcpy(parity, remainder, kParity);
}

// INFO: fc 22nov26 F.2 for station 0x00FFFE on table 9, fields packed from bit 0 as B.3 counts
constexpr uint8_t kStationHeader[5] = {0x04, 0x89, 0xFF, 0x3F, 0x00};

// A G.3 Traffic Uplink laid out from D.1.1, E.1 to E.3, F.2 and G.3 alone, with the parity given.
inline int spec_uplink(const Packet* traffic, int count, uint8_t network_header, bool fec,
                       uint8_t* packet) {
    uint8_t covered[223] = {network_header};
    uint8_t* data = covered + 1;
    std::memcpy(data, kStationHeader, sizeof(kStationHeader));
    int data_bytes = static_cast<int>(sizeof(kStationHeader));
    for (int i = 0; i < count; i++) {
        uint8_t* record = data + data_bytes;
        std::memcpy(record, traffic[i].payload + 1, 4);
        record[3] = static_cast<uint8_t>(record[3] & 0x3Fu);
        std::memcpy(record + 4, traffic[i].payload + 5, 15);
        data_bytes += 19;
    }
    while ((data_bytes - static_cast<int>(sizeof(kStationHeader)) - 3) % 4 != 0) data_bytes++;
    if (((network_header >> 5) & 0x03u) == 0) {
        uint32_t words[224 / 4];
        const uint32_t zero_key[4] = {0, 0, 0, 0};
        std::memcpy(words, data, static_cast<size_t>(data_bytes));
        spec_xxtea(words, static_cast<uint32_t>(data_bytes / 4), zero_key, 6);
        std::memcpy(data, words, static_cast<size_t>(data_bytes));
    }
    const int covered_bytes = 1 + data_bytes;
    std::memcpy(packet + 1, covered, static_cast<size_t>(covered_bytes));
    if (fec) {
        uint8_t block[223] = {0};
        std::memcpy(block + 223 - covered_bytes, covered, static_cast<size_t>(covered_bytes));
        spec_reed_solomon(block, packet + 1 + covered_bytes);
        packet[0] = static_cast<uint8_t>(covered_bytes + 32);
    } else {
        const uint32_t crc = spec_crc24(covered, static_cast<size_t>(covered_bytes));
        packet[1 + covered_bytes] = static_cast<uint8_t>(crc >> 16);
        packet[2 + covered_bytes] = static_cast<uint8_t>(crc >> 8);
        packet[3 + covered_bytes] = static_cast<uint8_t>(crc);
        packet[0] = static_cast<uint8_t>(covered_bytes + 3);
    }
    return 1 + packet[0];
}

}

#endif
