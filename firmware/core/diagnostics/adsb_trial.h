#ifndef SKYBLIP_CORE_DIAGNOSTICS_ADSB_TRIAL_H
#define SKYBLIP_CORE_DIAGNOSTICS_ADSB_TRIAL_H

#include <array>
#include <cstdint>

#include "core/diagnostics/record.h"

namespace skyblip::diagnostics {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
constexpr uint8_t kAdsbSlotFlagFrontEndRaw = 1u << 2;
constexpr uint8_t kAdsbSlotFlagManchester = 1u << 3;
constexpr uint8_t kAdsbSlotFlagFifoDropped = 1u << 4;

struct AdsbSlot {
    uint8_t profile{0};
    int8_t threshold_dbm{0};
    uint8_t gain_step{0};
    uint8_t boost{0};
    uint16_t frames{0};
    uint16_t crc{0};
    uint16_t unsupported{0};
    uint16_t folded{0};
    uint16_t positions{0};
    uint8_t aircraft{0};
    uint8_t counted_s{0};
    bool front_end_raw{false};
    bool manchester{false};
    bool fifo_dropped{false};
};

enum class AdsbChipsVerdict : uint8_t { Decoded = 0, Crc = 1, Unsupported = 2 };

constexpr uint8_t kAdsbChipsFlagTail = 1u << 2;
constexpr uint8_t kAdsbChipsFlagRssiValid = 1u << 3;
constexpr uint8_t kAdsbChipsVerdictShift = 4;
constexpr uint8_t kAdsbChipsVerdictMask = 0x03;
// INFO: fc 13nov26 27 chip bytes split 14 and 13, so the tail's last payload byte stays 0
constexpr uint32_t kAdsbChipsPerRecord = 14;

struct AdsbChips {
    uint8_t seq{0};
    int8_t rssi_dbm{0};
    AdsbChipsVerdict verdict{AdsbChipsVerdict::Decoded};
    std::array<uint8_t, kAdsbChipsPerRecord> chips{};
    bool tail{false};
    bool rssi_valid{false};
};

Record record_of(const AdsbSlot& value, const Instant& at);
Record record_of(const AdsbChips& value, const Instant& at);
bool read(const Record& record, AdsbSlot& out);
bool read(const Record& record, AdsbChips& out);

}  // namespace skyblip::diagnostics

#endif
