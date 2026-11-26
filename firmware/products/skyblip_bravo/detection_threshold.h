#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_DETECTION_THRESHOLD_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_DETECTION_THRESHOLD_H

#include <array>
#include <cstdint>

#include "protocols/adsb/message.h"

namespace skyblip::bravo {

// INFO: fc 26nov26 a constant false-alarm rate, as radar holds one, measured where no frame can be
class DetectionThreshold {
   public:
    // INFO: fc 26nov26 #293: -110 dBm decoded the most positions indoors, -114 let noise through
    static constexpr int16_t kFloorDbm = -110;
    // INFO: fc 26nov26 2 dB under -90 dBm, where cb-tfc's window soak (c07610c) heard nothing
    static constexpr int16_t kCeilingDbm = -92;
    static constexpr int16_t kStepDb = 1;
    // INFO: fc 26nov26 far above the band a step at a time would leave a flood running a minute
    static constexpr int16_t kFloodStepDb = 3;
    static constexpr uint32_t kFloodFactor = 4;
    // INFO: fc 26nov26 five seconds hold 800 false alarms at the ceiling, enough to tell the edges
    static constexpr uint32_t kFalseAlarmWindowMs = 5000;

    // INFO: fc 26nov26 a false trigger holds the packet handler for a frame's 120 us of airtime
    static constexpr uint32_t kFrameAirtimeUs = 120;
    // TODO: fc 26nov26 2% of the detector given to noise, a choice until a bench run measures it
    static constexpr uint32_t kOccupancyCeilingPermille = 20;
    static constexpr uint32_t kFalseAlarmCeilingHz =
        kOccupancyCeilingPermille * 1000 / kFrameAirtimeUs;
    static constexpr uint32_t kFalseAlarmFloorHz = kFalseAlarmCeilingHz / 8;

    // INFO: fc 26nov26 no transponder sends these downlink formats, so a frame naming one is noise
    static constexpr std::array<uint8_t, 12> kImpossibleFormats = {1, 2,  3,  6,  7,  8,
                                                                   9, 10, 12, 13, 14, 15};
    static constexpr uint32_t kFormatCodes = 32;

    static uint32_t impossible_of(const std::array<uint32_t, adsb::kDownlinkFormats>& formats);

    bool update(uint32_t impossible_frames, uint32_t now_ms);

    int16_t threshold_dbm() const { return threshold_dbm_; }
    uint32_t false_alarm_hz() const { return false_alarm_hz_; }

   private:
    int16_t threshold_dbm_{kFloorDbm};
    uint32_t false_alarm_hz_{0};
    uint32_t window_started_ms_{0};
    uint32_t window_impossible_{0};
    bool started_{false};
};

}  // namespace skyblip::bravo

#endif
