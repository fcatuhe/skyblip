#include "core/protocol/adsl.h"

#include <algorithm>

#include "core/fec/crc.h"
#include "core/fec/repair.h"
#include "core/fec/xxtea.h"
#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::protocol {

namespace {

constexpr uint16_t kFrameBits = AdslPacket::kDataBytes * 8;
constexpr int kMostErasures = 16;
constexpr int64_t kE7PerDegree = 10000000;

constexpr auto kCrcSyndromes = fec::syndromes<kFrameBits>([](uint16_t bit) {
    uint8_t frame[AdslPacket::kDataBytes] = {};
    frame[bit >> 3] = static_cast<uint8_t>(0x80u >> (bit & 7));
    return fec::adsl_crc_check(frame, AdslPacket::kDataBytes);
});

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

int32_t AdslPacket::latitude_1e7() const {
    return degrees_1e7_of(latitude_steps(), kLatitudeStepsPerDegree);
}
int32_t AdslPacket::longitude_1e7() const {
    return degrees_1e7_of(longitude_steps(), kLongitudeStepsPerDegree);
}
void AdslPacket::set_latitude_1e7(int32_t latitude) {
    set_field(kLatitudeField, steps_of(latitude, kLatitudeStepsPerDegree));
}
void AdslPacket::set_longitude_1e7(int32_t longitude) {
    set_field(kLongitudeField, steps_of(longitude, kLongitudeStepsPerDegree));
}

uint16_t AdslPacket::speed_q() const {
    return static_cast<uint16_t>(kSpeedCode.decode_middle(field(kSpeedField)));
}
// INFO: fc 09nov26 G.1.7 to G.1.9: a value past the limit reads the limit, never invalid
void AdslPacket::set_speed_q(uint16_t speed) {
    set_field(kSpeedField, short_of(kSpeedCode.encode(speed), kSpeedInvalidCode));
}

int32_t AdslPacket::altitude_m() const {
    return kAltitudeCode.decode_middle(field(kAltitudeField)) - kAltitudeOffsetM;
}
void AdslPacket::set_altitude_m(int32_t altitude) {
    const int32_t above_floor = std::max(altitude + kAltitudeOffsetM, 0);
    set_field(kAltitudeField, short_of(kAltitudeCode.encode(above_floor), kAltitudeInvalidCode));
}

int16_t AdslPacket::climb_e8() const {
    return static_cast<int16_t>(kClimbCode.decode_middle(field(kClimbField)));
}
void AdslPacket::set_climb_e8(int16_t climb) {
    set_field(kClimbField, short_of(kClimbCode.encode(climb), kClimbInvalidCode));
}

// INFO: fc 09nov26 a packed struct can sit at an odd address, so XXTEA works on an aligned copy
void AdslPacket::scramble() {
    uint32_t words[kPayloadBytes / 4];
    __builtin_memcpy(words, payload, sizeof(words));
    fec::xxtea_encrypt(words, kPayloadBytes / 4, fec::kXxteaZeroKey, 6);
    __builtin_memcpy(payload, words, sizeof(words));
}
void AdslPacket::descramble() {
    uint32_t words[kPayloadBytes / 4];
    __builtin_memcpy(words, payload, sizeof(words));
    fec::xxtea_decrypt(words, kPayloadBytes / 4, fec::kXxteaZeroKey, 6);
    __builtin_memcpy(payload, words, sizeof(words));
}

void AdslPacket::set_crc() {
    const uint32_t crc = fec::adsl_crc_compute(data, kCrcCoverBytes);
    crc_bytes[0] = static_cast<uint8_t>(crc >> 16);
    crc_bytes[1] = static_cast<uint8_t>(crc >> 8);
    crc_bytes[2] = static_cast<uint8_t>(crc);
}
uint32_t AdslPacket::check_crc() const { return fec::adsl_crc_check(data, kDataBytes); }

int AdslPacket::correct(const uint8_t* erasures, int max_erasures) {
    return fec::repair(data, erasures, check_crc(), kCrcSyndromes.check(),
                       std::min(max_erasures, kMostErasures), true);
}

bool to_observation(const AdslPacket& packet, const events::Stamp& received, int8_t rssi_dbm,
                    model::Source source, model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    if (!packet.is_position() || !packet.has_position()) return false;
    out.address = packet.address();
    out.address_mapping_table = packet.address_mapping_table();
    out.aircraft_category = packet.aircraft_category;
    out.flight_state = packet.flight_state;
    out.emergency = packet.emergency;
    out.latitude_1e7 = packet.latitude_1e7();
    out.longitude_1e7 = packet.longitude_1e7();
    out.altitude_valid = packet.has_altitude();
    out.altitude_m = out.altitude_valid ? packet.altitude_m() : 0;
    out.climb_valid = packet.has_climb();
    out.climb_e8 = out.climb_valid ? packet.climb_e8() : 0;
    out.speed_valid = packet.has_speed();
    out.speed_q = out.speed_valid ? packet.speed_q() : 0;
    out.track_c9 = packet.track_c9();
    out.received = received;
    out.rssi_dbm = rssi_dbm;
    out.source = source;
    out.position_valid = true;
    return true;
}

// ADS-L 4 SRD860 issue 2 G.1.14, NACp. Code 0 is "unknown or HFOM >= 0.5 NM".
uint8_t AdslPacket::horizontal_accuracy_code(uint32_t hfom_cm) {
    static constexpr uint32_t kLimitCm[] = {300, 1000, 3000, 9260, 18520, 55560, 92600};
    for (int i = 0; i < 7; i++)
        if (hfom_cm < kLimitCm[i]) return static_cast<uint8_t>(7 - i);
    return 0;
}

// G.1.15, GVA. 3 = VFOM < 10 m, 2 = < 45 m, 1 = < 150 m, 0 = unknown or worse.
uint8_t AdslPacket::vertical_accuracy_code(uint32_t vfom_cm) {
    if (vfom_cm < 1000) return 3;
    if (vfom_cm < 4500) return 2;
    if (vfom_cm < 15000) return 1;
    return 0;
}

// G.1.16, NACv. A GNSS-only velocity is as good as the position fix behind it,
// which is the relation the reference encoder uses verbatim
// (oss/SoftRF-moshe-braner .../libraries/OGN/ads-l.h:453).
uint8_t AdslPacket::velocity_accuracy_code(uint8_t horizontal_code) {
    return horizontal_code >= 4 ? static_cast<uint8_t>(horizontal_code - 4) : 0;
}

void AdslPacket::set_integrity_unknown() {
    source_integrity = 0;
    design_assurance = 0;
    navigation_integrity = 0;
    horizontal_accuracy = 0;
    vertical_accuracy = 0;
    velocity_accuracy = 0;
}

// INFO: fc 13sep26 no VDOP is a 2D solution, whose height is not a figure to claim accuracy for
void AdslPacket::set_integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2) {
    set_integrity_unknown();
    if (hdop_e2 == 0) return;
    const uint32_t hfom_cm = divide_rounded<uint32_t>(hdop_e2 * kMeritPerDopCm, 100);
    const uint32_t vfom_cm = divide_rounded<uint32_t>(vdop_e2 * kMeritPerDopCm, 100);
    horizontal_accuracy = horizontal_accuracy_code(hfom_cm);
    vertical_accuracy = vdop_e2 == 0 ? 0 : vertical_accuracy_code(vfom_cm);
    velocity_accuracy = velocity_accuracy_code(horizontal_accuracy);
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

bool from_own_status(AdslPacket& packet, settings::AirAddress address,
                     uint8_t address_mapping_table, const StatusReport& report) {
    if (!address) return false;
    packet.reset(AdslPacket::kPayloadTypeStatus);
    packet.version = AdslPacket::kProtocolVersionIssue2;
    packet.set_address(*address & settings::kAddressMask);
    packet.set_address_mapping_table(address_mapping_table);
    packet.set_field(AdslPacket::kMaxProtocolVersionField, AdslPacket::kProtocolVersionIssue2);
    packet.set_field(AdslPacket::kMBandReceiveField, static_cast<uint32_t>(report.m_band));
    packet.set_field(AdslPacket::kOBandLdrReceiveField, static_cast<uint32_t>(report.o_band_ldr));
    packet.set_field(AdslPacket::kOBandHdrReceiveField, static_cast<uint32_t>(report.o_band_hdr));
    packet.set_field(AdslPacket::kConspicuityReceiveField, report.conspicuity_received);
    packet.set_field(AdslPacket::kConspicuityTransmitField, report.conspicuity_transmitted);
    packet.set_field(AdslPacket::kTrafficUplinkClientField, report.traffic_uplink_client ? 1 : 0);
    return true;
}

bool is_callsign_character(char c) {
    return c == ' ' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool from_own_callsign(AdslPacket& packet, settings::AirAddress address,
                       uint8_t address_mapping_table, const char* callsign) {
    if (!address) return false;
    packet.reset(AdslPacket::kPayloadTypeOgnDiagnostics);
    packet.set_address(*address & settings::kAddressMask);
    packet.set_address_mapping_table(address_mapping_table);
    packet.set_information_header(AdslPacket::kTelemetryTypeInformation,
                                  AdslPacket::kInformationTypeRegistration);

    char* text = packet.information_text();
    int count = 0;
    while (count < AdslPacket::kInformationTextBytes && callsign[count] != 0 &&
           is_callsign_character(callsign[count])) {
        text[count] = callsign[count];
        count++;
    }
    while (count < AdslPacket::kInformationTextBytes) text[count++] = 0;
    return true;
}

int callsign_of(const AdslPacket& packet, char* out, int capacity) {
    out[0] = 0;
    if (!packet.is_registration() || capacity <= 1) return 0;
    const char* text = packet.information_text();
    int count = 0;
    while (count < AdslPacket::kInformationTextBytes && text[count] != 0) {
        if (!is_callsign_character(text[count])) return 0;
        count++;
    }
    if (count >= capacity) count = capacity - 1;
    for (int i = 0; i < count; i++) out[i] = text[i];
    out[count] = 0;
    return count;
}

bool from_own(AdslPacket& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category) {
    return from_own(packet, own, address, address_mapping_table, aircraft_category,
                    BurstInstant{own.utc_s, 0, 0});
}

bool from_own(AdslPacket& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category, const BurstInstant& at) {
    if (!address) return false;
    packet.reset(AdslPacket::kPayloadTypePosition);
    packet.set_address(*address & settings::kAddressMask);
    packet.set_address_mapping_table(address_mapping_table);

    // The burst leaves later than the fix was solved, so the position goes
    // forward to the instant the timestamp names. Past the model's bound the
    // fix goes out as it stands, dated when it was solved: neither half of the
    // pair is allowed to describe an instant the other does not.
    const int32_t named_ms =
        at.into_utc_ms - at.into_utc_ms % static_cast<int32_t>(kTimestampQuarterMs);
    const flight::Prediction where =
        flight::extrapolate(own, at.since_fix_ms - (at.into_utc_ms - named_ms));
    packet.timestamp =
        timestamp_code(at.utc_s, where.valid ? named_ms : at.into_utc_ms - at.since_fix_ms);
    packet.flight_state = flight::announced_state(own.flight_state, aircraft_category);
    packet.aircraft_category = aircraft_category;
    packet.emergency = 1;
    packet.set_latitude_1e7(where.latitude_1e7);
    packet.set_longitude_1e7(where.longitude_1e7);

    // Altitude and ground speed come from the 3D fix, so without one they are
    // UNAVAILABLE and must say so (G.1.7, G.1.8). Transmitting a stale or zeroed
    // altitude as if it were valid is worse than transmitting nothing: a receiver
    // would compute relative vertical separation against it.
    if (own.fix_valid) {
        if (own.vdop_e2 != 0)
            packet.set_altitude_m(to_metres(Millimetres(where.altitude_hae_mm)).value);
        else
            packet.set_altitude_invalid();
        packet.set_speed_q(to_speed_q(MillimetresPerSecond(own.speed_mm_s)).value);
        packet.set_integrity_from_dop_e2(own.hdop_e2, own.vdop_e2);
    } else {
        packet.set_position_invalid();
        packet.set_altitude_invalid();
        packet.set_speed_invalid();
        packet.set_integrity_unknown();
    }

    // Vertical rate needs two samples over a window, so it arrives later than the
    // fix and has its own validity. Encoding 0 before then would claim level
    // flight (G.1.9).
    if (own.climb_valid)
        packet.set_climb_e8(to_climb_e8(MillimetresPerSecond(own.climb_mm_s)).value);
    else
        packet.set_climb_invalid();

    packet.set_track_c9(to_cordic9(CentiDegrees(where.track_cdeg)).value);
    return true;
}

}
