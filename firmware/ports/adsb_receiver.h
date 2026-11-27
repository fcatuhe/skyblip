#ifndef SKYBLIP_PORTS_ADSB_RECEIVER_H
#define SKYBLIP_PORTS_ADSB_RECEIVER_H

#include <algorithm>
#include <cstdint>

#include "core/util/result.h"
#include "protocols/adsb/message.h"

namespace skyblip::ports {

// INFO: fc 26nov26 the LR2021's own codes, DS 6.7.1 table 6-38, so a capture reads what the part
// said
enum class ReceiverMode : uint8_t {
    Sleep = 0,
    StandbyRc = 1,
    StandbyXosc = 2,
    Synthesis = 3,
    Receive = 4,
    Transmit = 5,
    Unknown = 6
};

enum class ReceiverReset : uint8_t { None = 0, PowerOn = 1, Pin = 2, Unknown = 3 };

struct ReceiverProbe {
    uint32_t packets{0};
    uint16_t errors{0};
    int8_t noise_dbm{0};
    int8_t threshold_dbm{0};
    ReceiverMode mode{ReceiverMode::Unknown};
    ReceiverReset reset{ReceiverReset::None};
    uint8_t gain_step{0};
    bool interrupt_pending{false};
    bool front_end_kept{false};
    bool valid{false};
};

struct AdsbReceiverStatistics {
    static constexpr int kRssiBuckets = 8;
    // INFO: fc 11nov26 the floor under each bucket in dBm: the detection threshold, then 10 dB
    // steps
    static constexpr int8_t kRssiFloorsDbm[kRssiBuckets] = {-128, -90, -80, -70,
                                                            -60,  -50, -40, -30};
    static constexpr uint64_t kSecondUs = 1000000;

    uint32_t frames{0};
    uint32_t frame_rate_hz{0};
    uint32_t dropped{0};
    uint32_t rearms{0};
    uint32_t resets{0};
    uint32_t command_errors{0};
    uint16_t device_errors{0};
    uint32_t missed_edges{0};
    uint32_t part_resets{0};
    uint32_t framing_resyncs{0};
    uint32_t overflow_drops{0};
    uint32_t resync_drops{0};
    uint32_t queue_drops{0};
    uint32_t in_flight{0};
    uint32_t split_counts{0};
    uint32_t stranded_resyncs{0};
    uint32_t short_resyncs{0};
    uint32_t drains{0};
    uint64_t drain_busy_us{0};
    uint32_t formats[adsb::kDownlinkFormats]{};
    uint32_t squitters{0};
    uint32_t all_calls{0};
    uint32_t replies{0};
    uint32_t format_refused{0};
    uint32_t parity_refused{0};
    uint32_t one_bit_errors{0};
    uint32_t fifo_peak_frames{0};
    uint32_t queue_peak_frames{0};
    uint32_t worst_drain_us{0};
    uint32_t irq_wakes{0};
    uint32_t empty_wakes{0};
    uint64_t poll_us{0};
    uint64_t screen_us{0};
    uint64_t busy_wait_us{0};
    uint32_t transactions{0};
    // INFO: fc 21nov26 false means the thread polls every 5 ms, since the IRQ edge never armed
    bool irq_armed{false};
    ReceiverProbe probe{};
    uint32_t rssi[kRssiBuckets]{};
    uint64_t counting_second{0};
    uint32_t counted{0};
    uint32_t fifo_peak_counting{0};
    uint32_t queue_peak_counting{0};
    uint32_t worst_drain_counting{0};

    // INFO: fc 27nov26 counts every frame read, repairs one bit if it can, and gives the verdict
    adsb::Screened admit(uint8_t* frame, int8_t rssi_dbm, bool rssi_valid, uint64_t at_us) {
        heard(rssi_dbm, rssi_valid, at_us);
        const uint8_t format = frame[0] >> 3;
        formats[std::min<uint8_t>(format, adsb::kDownlinkFormats - 1)]++;
        adsb::Screened screened = adsb::screen(frame);
        if (screened == adsb::Screened::Parity && adsb::repair_one_bit(frame))
            screened = adsb::Screened::Repaired;
        switch (screened) {
            case adsb::Screened::Squitter: squitters++; break;
            case adsb::Screened::AllCall: all_calls++; break;
            case adsb::Screened::Reply: replies++; break;
            case adsb::Screened::Repaired: one_bit_errors++; break;
            case adsb::Screened::Format: format_refused++; break;
            case adsb::Screened::Parity: parity_refused++; break;
        }
        return screened;
    }

    void drained(uint32_t fifo_frames, uint32_t queue_frames, uint32_t busy_us, uint64_t at_us) {
        roll(at_us);
        fifo_peak_counting = std::max(fifo_peak_counting, fifo_frames);
        queue_peak_counting = std::max(queue_peak_counting, queue_frames);
        worst_drain_counting = std::max(worst_drain_counting, busy_us);
    }

    // INFO: fc 11nov26 rates and peaks are the last whole second, so a silent second reads zero
    void roll(uint64_t now_us) {
        const uint64_t second = now_us / kSecondUs;
        if (second == counting_second) return;
        const bool consecutive = second == counting_second + 1;
        frame_rate_hz = consecutive ? counted : 0;
        fifo_peak_frames = consecutive ? fifo_peak_counting : 0;
        queue_peak_frames = consecutive ? queue_peak_counting : 0;
        worst_drain_us = consecutive ? worst_drain_counting : 0;
        counting_second = second;
        counted = 0;
        fifo_peak_counting = 0;
        queue_peak_counting = 0;
        worst_drain_counting = 0;
    }

    void heard(int8_t rssi_dbm, bool rssi_valid, uint64_t at_us) {
        roll(at_us);
        frames++;
        counted++;
        if (rssi_valid) rssi[bucket_of(rssi_dbm)]++;
    }

    static int bucket_of(int8_t rssi_dbm) {
        int bucket = 0;
        while (bucket + 1 < kRssiBuckets && rssi_dbm >= kRssiFloorsDbm[bucket + 1]) bucket++;
        return bucket;
    }
};

class AdsbReceiver {
   public:
    virtual ~AdsbReceiver() = default;

    virtual Status begin() = 0;
    virtual AdsbReceiverStatistics statistics() const = 0;
    // INFO: fc 11nov26 armed on 1090 MHz: false before begin(), through a reset, once parked
    virtual bool listening() const = 0;
    // INFO: fc 11nov26 terminal like ports::Rf::sleep(): parked() turns true only once the part
    // confirmed its sleep
    virtual void sleep() = 0;
    virtual bool parked() const = 0;
    // INFO: fc 11nov26 the shutdown's fallback past its ceiling: the part is held in reset, which
    // receives nothing
    virtual void abandon() = 0;
};

}  // namespace skyblip::ports

#endif
