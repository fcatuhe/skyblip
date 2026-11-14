#ifndef SKYBLIP_HARDWARE_PLATFORM_CLEAR_CHANNEL_H
#define SKYBLIP_HARDWARE_PLATFORM_CLEAR_CHANNEL_H

#include <cstdint>

#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "ports/clock.h"
#include "ports/rf.h"

namespace skyblip::platform {

// INFO: fc 14nov26 EN 300 220-1 Table 45 for this antenna, read by the SX1262 behind its feed
constexpr int32_t kCcaThresholdCentiDbm = timing::cca_threshold_centi_dbm(
    parts::sx1262::kAntennaGainCentiDbd, parts::sx1262::kFeedLossCentiDb);

// INFO: fc 03oct26 build 892 staged 0.42-0.66 ms past the stage point: 700 left 40 us spare
constexpr uint64_t kTxStageLeadUs = 700;
// TODO: fc 14nov26 a guess at the CCA reads' overrun, until a bench capture times them on nRF52
constexpr uint64_t kCcaOverrunUs = 300;
// INFO: fc 14nov26 the CCA's window, then staging, so it listens on the dwell's channel in RX
constexpr uint64_t kCcaLeadUs = kTxStageLeadUs + timing::PoliteAccess::kCcaWindowUs + kCcaOverrunUs;
static_assert(kCcaLeadUs <= timing::PoliteAccess::kCcaLeadUs &&
                  kCcaLeadUs - timing::PoliteAccess::kCcaWindowUs <
                      timing::PoliteAccess::kDeadTimeCeilingUs,
              "the assessment starts inside the dwell and ends inside the declared dead time");

struct Assessment {
    bool clear{false};
    bool level_valid{false};
    int8_t level_dbm{0};
    uint64_t ended_us{0};
};

// INFO: fc 14nov26 EN 300 220-1 5.21.1: a radio not in RX cannot call the channel clear
template <class ReadLevel>
Assessment assess(bool receiving, ReadLevel read_level, const ports::Clock& clock) {
    Assessment assessment{};
    assessment.level_valid = receiving;
    if (receiving) {
        assessment.level_dbm = read_level();
        assessment.clear = timing::channel_clear(assessment.level_dbm, kCcaThresholdCentiDbm);
    }
    assessment.ended_us = clock.microseconds();
    return assessment;
}

// INFO: fc 14nov26 EN 300 220-1 Table 48's dead time, the CCA's end to the burst on air
inline bool within_dead_time(const Assessment& assessment, uint64_t now_us) {
    return now_us - assessment.ended_us <= timing::PoliteAccess::kDeadTimeCeilingUs;
}

inline events::RfEvent busy_report(const ports::RfBurst& burst, const Assessment& assessment,
                                   model::Band band, uint32_t frequency_hz, uint64_t at_us) {
    events::RfEvent busy{};
    busy.type = events::RfEventType::Busy;
    busy.band = band;
    busy.frequency_hz = frequency_hz;
    busy.rssi_dbm = assessment.level_dbm;
    busy.rssi_valid = assessment.level_valid;
    busy.at_us = at_us;
    busy.tx_at_us = burst.at_us;
    return busy;
}

}  // namespace skyblip::platform

#endif
