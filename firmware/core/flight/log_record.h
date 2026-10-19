// core/flight/log_record.h: what one logged instant looks like on flash, and
// where it sits in the sector core/store labelled. Byte-exact: the tablet
// and the host script that turn these records into an IGC file read the same
// layout, so it is written out field by field rather than memcpy'd off a packed
// struct whose padding nobody can see.
//
// The device never writes IGC. An IGC file needs a header block, a manufacturer
// code, a G-record signature and a text encoding of every fix; all of that is
// work a tablet or a laptop does for free, and none of it survives a power cut
// any better than these records do. What the device owes is the flight, whole
// and verifiable. The conversion lives off the device.
#ifndef SKYBLIP_CORE_FLIGHT_LOG_RECORD_H
#define SKYBLIP_CORE_FLIGHT_LOG_RECORD_H

#include <cstddef>
#include <cstdint>

#include "core/model/ownship.h"
#include "core/store/sector.h"
#include "core/util/result.h"

namespace skyblip::flight {

constexpr uint32_t kLogRecordBytes = 24;

// Both candidate external parts (MX25R1635F and ZD25WQ16B) erase in 4 KB
// sectors, which is also the erase unit the devicetree log_partition is cut on.
constexpr uint32_t kLogSectorBytes = 4096;

constexpr uint32_t log_slots_per_sector(uint32_t sector_bytes) {
    return sector_bytes <= store::kSectorHeaderBytes
               ? 0
               : (sector_bytes - store::kSectorHeaderBytes) / kLogRecordBytes;
}

// 170 records in a 4 KB sector, and 16 bytes of label: 0.4% overhead.
constexpr uint32_t kLogSlotsPerSector = log_slots_per_sector(kLogSectorBytes);

constexpr uint32_t log_record_offset(uint32_t slot) {
    return store::kSectorHeaderBytes + slot * kLogRecordBytes;
}

// Bit 5 and 6 carry the ADS-L 4 SRD860 issue 2 G.1.2 flight state code as it
// stood when the record was taken, so a reader can see the takeoff and the
// landing the session was cut on rather than infer them from the speeds.
constexpr uint8_t kLogFlagFixValid = 1u << 0;
constexpr uint8_t kLogFlagUtcValid = 1u << 1;
constexpr uint8_t kLogFlagPpsLocked = 1u << 2;
constexpr uint8_t kLogFlagClimbValid = 1u << 3;
constexpr uint8_t kLogFlagGeoidMeasured = 1u << 4;
constexpr uint8_t kLogFlagFlightStateShift = 5;
constexpr uint8_t kLogFlagFlightStateMask = 0x03;
// The one record a clean landing writes. Its absence is how a reader tells a
// flight that ended from a flight the battery ended.
constexpr uint8_t kLogFlagSessionEnd = 1u << 7;

// One logged instant, in the units own-ship already carries. utc_s is absolute
// here and relative on flash: the sector header holds the base.
struct LogRecord {
    uint32_t utc_s{0};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_msl_m{0};
    int32_t altitude_hae_m{0};
    uint16_t speed_q{0};
    uint16_t track_c9{0};
    int16_t climb_e8{0};
    uint16_t hdop_e2{0};
    uint8_t satellites{0};
    uint8_t flight_state{0};
    bool fix_valid{false};
    bool utc_valid{false};
    bool pps_locked{false};
    bool climb_valid{false};
    bool geoid_measured{false};
    bool session_end{false};
};

// INFO: fc 15oct26 a fix's track is nine bits, so the top bit of byte 16 marks an event instead
constexpr uint32_t kLogKindOffset = 16;
constexpr uint8_t kLogEventMark = 0x80;

// INFO: fc 15oct26 a wire format: a kind is retired, never renumbered
enum class LogKind : uint8_t { Fix = 0, Status = 1, Alarm = 2, Encounter = 3 };

struct LogStatus {
    uint16_t cell_mv{0};
    uint8_t percent{0};
    uint8_t power_level{0};
    uint8_t charge{0};
    uint8_t tracked{0};
    uint8_t tracked_peak{0};
    bool battery_valid{false};
    bool charging{false};
    bool external_power{false};
};

struct LogTarget {
    uint32_t address{0};
    uint8_t address_mapping_table{0};
    uint8_t aircraft_category{0};
    uint16_t distance_m{0};
    int16_t relative_altitude_m{0};
    uint16_t relative_bearing_deg{0};
    int16_t closing_mps{0};
    uint8_t alarm{0};
    uint8_t source{0};
    int8_t rssi_dbm{0};
    uint16_t heard_s{0};
    bool dismissed{false};
    bool in_formation{false};
};

struct LogEvent {
    LogKind kind{LogKind::Status};
    uint32_t utc_s{0};
    LogStatus status{};
    LogTarget target{};
};

// Everything worth keeping out of the state the whole device already agrees on.
LogRecord log_record_from(const model::Ownship& own);

// 24 bytes, little-endian, CRC-16-CCITT over the first 22. base_utc_s is the
// session's opening second: a record more than 18 hours into a session saturates
// its offset rather than wrapping into the past.
void encode_log_record(const LogRecord& record, uint32_t base_utc_s, uint8_t* out);

// Status::Empty for a slot that was never written (erased flash reads 0xFF),
// Status::Crc for one that was being written when the power went, Unsupported
// for an event, Ok otherwise. A torn record is never handed back as a position.
Status decode_log_record(const uint8_t* raw, uint32_t base_utc_s, LogRecord& out);

void encode_log_event(const LogEvent& event, uint32_t base_utc_s, uint8_t* out);
// INFO: fc 15oct26 Unsupported for a fix or an unknown kind, so neither is read as the other
Status decode_log_event(const uint8_t* raw, uint32_t base_utc_s, LogEvent& out);

// True when every byte is 0xFF, which on NOR means nothing has been programmed
// here. Checked before the CRC, so an erased slot is never a checksum question.
bool log_slot_erased(const uint8_t* raw, uint32_t bytes);

}  // namespace skyblip::flight

#endif
