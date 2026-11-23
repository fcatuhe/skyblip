#include "protocols/adsl/packet.h"

#include <algorithm>
#include <cstddef>

#include "protocols/common/crc.h"
#include "protocols/common/geometry.h"
#include "protocols/common/repair.h"
#include "protocols/common/xxtea.h"

namespace skyblip::adsl {

using wire::divide_rounded;

namespace {

// INFO: fc 13nov26 Issue 1 reserves 14 up, Issue 2 names 14 to 17 from model plane to paramotor
constexpr uint8_t kFirstIssue2Category = 14;
constexpr uint8_t kLastIssue2Category = 17;
constexpr uint16_t kFrameBits = Packet::kDataBytes * 8;
constexpr int kMostErasures = 16;
constexpr int64_t kE7PerDegree = 10000000;

constexpr auto kCrcSyndromes = wire::syndromes<kFrameBits>([](uint16_t bit) {
    uint8_t frame[Packet::kDataBytes] = {};
    frame[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
    return wire::crc24_check(frame, Packet::kDataBytes);
});
constexpr auto kStatusCrcSyndromes =
    wire::syndromes<Packet::kStatusDataBytes * 8>([](uint16_t bit) {
        uint8_t frame[Packet::kStatusDataBytes] = {};
        frame[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
        return wire::crc24_check(frame, Packet::kStatusDataBytes);
    });

uint8_t nearer_length(uint8_t length_heard) {
    const int to_status = __builtin_popcount(length_heard ^ Packet::kStatusDataBytes);
    const int to_traffic = __builtin_popcount(length_heard ^ Packet::kDataBytes);
    return to_status < to_traffic ? Packet::kStatusDataBytes : Packet::kDataBytes;
}

// INFO: fc 18nov26 past a 16-byte burst no transmitter is heard, so most of those chips are marked
constexpr int kTailMarkedCeiling = 16;

bool heard_to_full_length(const uint8_t* erasures) {
    int marked = 0;
    for (int i = Packet::kStatusDataBytes; i < Packet::kDataBytes; i++)
        marked += __builtin_popcount(erasures[i]);
    return marked <= kTailMarkedCeiling;
}

wire::Status receive_at(const uint8_t* frame, const uint8_t* erasures, uint8_t data_bytes,
                        Packet& out) {
    if (data_bytes == Packet::kDataBytes && !heard_to_full_length(erasures)) {
        out.reset();
        return wire::Status::Crc;
    }
    return receive(frame, erasures, out, data_bytes);
}

uint32_t steps_of(int32_t degrees_1e7, int64_t steps_per_degree) {
    const int64_t steps = divide_rounded<int64_t>(degrees_1e7 * steps_per_degree, kE7PerDegree);
    return static_cast<uint32_t>(steps) & 0xFFFFFFu;
}

int32_t degrees_1e7_of(int32_t steps, int64_t steps_per_degree) {
    return static_cast<int32_t>(divide_rounded<int64_t>(steps * kE7PerDegree, steps_per_degree));
}

uint32_t short_of(uint32_t code, uint32_t invalid_code) {
    return code >= invalid_code ? invalid_code - 1 : code;
}

}

int32_t Packet::latitude_1e7() const {
    return degrees_1e7_of(latitude_steps(), kLatitudeStepsPerDegree);
}
int32_t Packet::longitude_1e7() const {
    return degrees_1e7_of(longitude_steps(), kLongitudeStepsPerDegree);
}
void Packet::set_latitude_1e7(int32_t latitude) {
    set_field(kLatitudeField, steps_of(latitude, kLatitudeStepsPerDegree));
}
void Packet::set_longitude_1e7(int32_t longitude) {
    set_field(kLongitudeField, steps_of(longitude, kLongitudeStepsPerDegree));
}

uint16_t Packet::speed_q() const {
    return static_cast<uint16_t>(kSpeedCode.decode_middle(field(kSpeedField)));
}
// INFO: fc 09nov26 G.1.7 to G.1.9: a value past the limit reads the limit, never invalid
void Packet::set_speed_q(uint16_t speed) {
    set_field(kSpeedField, short_of(kSpeedCode.encode(speed), kSpeedInvalidCode));
}

int32_t Packet::altitude_m() const {
    return kAltitudeCode.decode_middle(field(kAltitudeField)) - kAltitudeOffsetM;
}
void Packet::set_altitude_m(int32_t altitude) {
    const int32_t above_floor = std::max(altitude + kAltitudeOffsetM, 0);
    set_field(kAltitudeField, short_of(kAltitudeCode.encode(above_floor), kAltitudeInvalidCode));
}

int16_t Packet::climb_e8() const {
    return static_cast<int16_t>(kClimbCode.decode_middle(field(kClimbField)));
}
void Packet::set_climb_e8(int16_t climb) {
    set_field(kClimbField, short_of(kClimbCode.encode(climb), kClimbInvalidCode));
}

// INFO: fc 09nov26 a packed struct can sit at an odd address, so XXTEA works on an aligned copy
void Packet::scramble(uint8_t data_bytes) {
    uint32_t words[kPayloadBytes / 4];
    const uint32_t count = static_cast<uint32_t>(data_bytes - 4) / 4;
    const size_t bytes = size_t{count} * 4;
    __builtin_memcpy(words, payload, bytes);
    wire::xxtea_encrypt(words, count, wire::kXxteaZeroKey, 6);
    __builtin_memcpy(payload, words, bytes);
}
void Packet::descramble(uint8_t data_bytes) {
    uint32_t words[kPayloadBytes / 4];
    const uint32_t count = static_cast<uint32_t>(data_bytes - 4) / 4;
    const size_t bytes = size_t{count} * 4;
    __builtin_memcpy(words, payload, bytes);
    wire::xxtea_decrypt(words, count, wire::kXxteaZeroKey, 6);
    __builtin_memcpy(payload, words, bytes);
}

void Packet::set_crc(uint8_t data_bytes) {
    const uint32_t crc = wire::crc24_compute(data, data_bytes - 3u);
    data[data_bytes - 3] = static_cast<uint8_t>(crc >> 16);
    data[data_bytes - 2] = static_cast<uint8_t>(crc >> 8);
    data[data_bytes - 1] = static_cast<uint8_t>(crc);
}
bool Packet::crc_ok(uint8_t data_bytes) const { return wire::crc24_check(data, data_bytes) == 0; }

int Packet::correct(const uint8_t* erasures, int max_erasures, uint8_t data_bytes) {
    const wire::LinearCheck check =
        data_bytes == kStatusDataBytes ? kStatusCrcSyndromes.check() : kCrcSyndromes.check();
    return wire::repair(data, erasures, wire::crc24_check(data, data_bytes), check,
                        std::min(max_erasures, kMostErasures), true);
}

// INFO: fc 18nov26 E.1: the CRC also covers the header, so it is read only once the frame checks
wire::Status receive(const uint8_t* frame, const uint8_t* erasures, Packet& out,
                     uint8_t data_bytes) {
    out.reset();
    if (!Packet::supports_data_bytes(data_bytes)) return wire::Status::Invalid;
    __builtin_memcpy(out.data, frame, data_bytes);
    if (out.correct(erasures, 6, data_bytes) < 0) return wire::Status::Crc;
    if (data_bytes < Packet::kDataBytes)
        for (uint8_t i = data_bytes - 3; i < data_bytes; i++) out.data[i] = 0;
    if ((out.version & Packet::kErrorControlFec) != 0) return wire::Status::Invalid;
    if (out.protocol_version() > Packet::kMaxProtocolVersion) return wire::Status::Unsupported;
    if (out.key_index() == Packet::kKeyScrambled)
        out.descramble(data_bytes);
    else if (out.key_index() != Packet::kKeyClear)
        return wire::Status::Unsupported;
    return out.data_bytes() == data_bytes ? wire::Status::Ok : wire::Status::Invalid;
}

// INFO: fc 22nov26 a clean length names the only codeword the receiver can check
wire::Status receive_as_heard(const uint8_t* frame, const uint8_t* erasures, uint8_t length_heard,
                              bool length_doubtful, Packet& out) {
    if (!length_doubtful) return receive_at(frame, erasures, length_heard, out);

    const uint8_t nearer = nearer_length(length_heard);
    const uint8_t other =
        nearer == Packet::kDataBytes ? Packet::kStatusDataBytes : Packet::kDataBytes;
    Packet at_other;
    const wire::Status heard = receive_at(frame, erasures, nearer, out);
    const wire::Status other_heard = receive_at(frame, erasures, other, at_other);
    if (heard == wire::Status::Ok && other_heard == wire::Status::Ok) {
        out.reset();
        return wire::Status::Invalid;
    }
    if (heard == wire::Status::Ok) return heard;
    if (other_heard == wire::Status::Ok) {
        out = at_other;
        return other_heard;
    }
    return heard;
}

// INFO: fc 20nov26 G.1.14 NACp thresholds, where code 0 is "unknown or HFOM >= 0.5 NM"
uint8_t Packet::horizontal_accuracy_code(uint32_t hfom_cm) {
    static constexpr uint32_t kLimitCm[] = {300, 1000, 3000, 9260, 18520, 55560, 92600};
    for (int i = 0; i < 7; i++)
        if (hfom_cm < kLimitCm[i]) return static_cast<uint8_t>(7 - i);
    return 0;
}

// INFO: fc 20nov26 G.1.15 GVA thresholds, where code 0 is unknown or a VFOM of 150 m or more
uint8_t Packet::vertical_accuracy_code(uint32_t vfom_cm) {
    if (vfom_cm < 1000) return 3;
    if (vfom_cm < 4500) return 2;
    if (vfom_cm < 15000) return 1;
    return 0;
}

uint8_t Packet::issue1_vertical_accuracy_code(uint32_t vfom_cm) {
    if (vfom_cm < 1500) return 3;
    return vertical_accuracy_code(vfom_cm);
}

bool issue2_category(uint8_t aircraft_category) {
    return aircraft_category >= kFirstIssue2Category && aircraft_category <= kLastIssue2Category;
}

uint8_t lowest_version(const Packet& packet) {
    if (!packet.is_position()) return Packet::kProtocolVersionIssue2;
    const bool issue2_codes =
        !packet.has_altitude() || !packet.has_speed() || !packet.has_climb() ||
        packet.field(Packet::kLatitudeField) == Packet::kPositionInvalidCode ||
        packet.field(Packet::kLongitudeField) == Packet::kPositionInvalidCode ||
        packet.field(Packet::kLatitudeField) == Packet::kIssue1PositionInvalidCode ||
        packet.field(Packet::kLongitudeField) == Packet::kIssue1PositionInvalidCode ||
        issue2_category(packet.aircraft_category);
    return issue2_codes ? Packet::kProtocolVersionIssue2 : Packet::kProtocolVersionIssue1;
}

uint32_t hfom_cm(uint16_t hdop_e2) {
    return divide_rounded<uint32_t>(uint32_t{hdop_e2} * 2u * kUereCm, 100u);
}

void Packet::set_integrity_unknown() {
    source_integrity = 0;
    design_assurance = 0;
    navigation_integrity = 0;
    horizontal_accuracy = 0;
    vertical_accuracy = 0;
    velocity_accuracy = 0;
}

uint8_t timestamp_code(uint32_t utc_s, int32_t lead_ms) {
    constexpr int64_t kCycleMs = static_cast<int64_t>(kTimestampCycleS) * 1000;
    int64_t ms = static_cast<int64_t>(utc_s % kTimestampCycleS) * 1000 + lead_ms;
    ms %= kCycleMs;
    if (ms < 0) ms += kCycleMs;
    return static_cast<uint8_t>(ms / kTimestampQuarterMs);
}

ReceiveClass receive_class(int heard_permille) {
    if (heard_permille > 800) return ReceiveClass::Full;
    if (heard_permille > 400) return ReceiveClass::Partial;
    if (heard_permille > 50) return ReceiveClass::Occasional;
    return ReceiveClass::None;
}

void encode_status(Packet& packet, uint32_t address, uint8_t address_mapping_table,
                   const StatusReport& report) {
    packet.reset(Packet::kPayloadTypeStatus);
    packet.set_protocol_version(Packet::kProtocolVersionIssue2);
    packet.set_address(address);
    packet.set_address_mapping_table(address_mapping_table);
    packet.set_field(Packet::kMaxProtocolVersionField, Packet::kMaxProtocolVersion);
    packet.set_field(Packet::kMBandReceiveField, static_cast<uint32_t>(report.m_band));
    packet.set_field(Packet::kOBandLdrReceiveField, static_cast<uint32_t>(report.o_band_ldr));
    packet.set_field(Packet::kOBandHdrReceiveField, static_cast<uint32_t>(report.o_band_hdr));
    packet.set_field(Packet::kConspicuityReceiveField, report.conspicuity_received);
    packet.set_field(Packet::kConspicuityTransmitField, report.conspicuity_transmitted);
    packet.set_field(Packet::kTransponderField, static_cast<uint32_t>(report.transponder));
    packet.set_field(Packet::kTrafficUplinkClientField, report.traffic_uplink_client ? 1 : 0);
}

void encode_traffic(Packet& packet, uint8_t address_mapping_table, const Traffic& traffic) {
    packet.reset(Packet::kPayloadTypePosition);
    packet.set_address(traffic.address);
    packet.set_address_mapping_table(address_mapping_table);
    packet.timestamp = traffic.timestamp;
    packet.flight_state = traffic.flight_state;
    packet.aircraft_category = traffic.aircraft_category;
    packet.emergency = traffic.emergency;
    if (traffic.position_valid) {
        packet.set_latitude_1e7(traffic.latitude_1e7);
        packet.set_longitude_1e7(traffic.longitude_1e7);
    } else {
        packet.set_position_invalid();
    }
    if (traffic.altitude_valid)
        packet.set_altitude_m(traffic.altitude_m);
    else
        packet.set_altitude_invalid();
    if (traffic.speed_valid)
        packet.set_speed_q(traffic.speed_q);
    else
        packet.set_speed_invalid();
    if (traffic.climb_valid)
        packet.set_climb_e8(traffic.climb_e8);
    else
        packet.set_climb_invalid();
    packet.set_track_c9(traffic.track_c9);
    packet.source_integrity = traffic.integrity.source_integrity;
    packet.design_assurance = traffic.integrity.design_assurance;
    packet.navigation_integrity = traffic.integrity.navigation_integrity;
    packet.horizontal_accuracy = traffic.integrity.horizontal_accuracy;
    packet.vertical_accuracy = traffic.integrity.vertical_accuracy;
    packet.velocity_accuracy = traffic.integrity.velocity_accuracy;
    packet.set_protocol_version(traffic.integrity.vertical_accuracy_issue2_only
                                    ? Packet::kProtocolVersionIssue2
                                    : lowest_version(packet));
}

wire::Status decode_traffic(const Packet& packet, Traffic& out) {
    out = Traffic{};
    if (!packet.is_position()) return wire::Status::Unsupported;
    out.address = packet.address();
    out.timestamp = packet.timestamp;
    out.flight_state = packet.flight_state;
    out.aircraft_category = packet.aircraft_category;
    out.emergency = packet.emergency;
    out.position_valid = packet.has_position();
    out.latitude_1e7 = packet.latitude_1e7();
    out.longitude_1e7 = packet.longitude_1e7();
    out.altitude_valid = packet.has_altitude();
    out.altitude_m = out.altitude_valid ? packet.altitude_m() : 0;
    out.speed_valid = packet.has_speed();
    out.speed_q = out.speed_valid ? packet.speed_q() : 0;
    out.climb_valid = packet.has_climb();
    out.climb_e8 = out.climb_valid ? packet.climb_e8() : 0;
    out.track_c9 = packet.track_c9();
    out.integrity.source_integrity = packet.source_integrity;
    out.integrity.design_assurance = packet.design_assurance;
    out.integrity.navigation_integrity = packet.navigation_integrity;
    out.integrity.horizontal_accuracy = packet.horizontal_accuracy;
    out.integrity.vertical_accuracy = packet.vertical_accuracy;
    out.integrity.velocity_accuracy = packet.velocity_accuracy;
    return wire::Status::Ok;
}

}
