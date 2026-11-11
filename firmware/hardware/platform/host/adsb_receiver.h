#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_ADSB_RECEIVER_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_ADSB_RECEIVER_H

#include "core/bus/bus.h"
#include "core/events/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "ports/adsb_receiver.h"
#include "ports/clock.h"
#include "runtime/tasks.h"

namespace skyblip::platform::host {

// INFO: fc 12nov26 the silicon thread's drain, run from the board's poll: the host has no IRQ edge
class AdsbReceiver : public ports::AdsbReceiver {
   public:
    AdsbReceiver(parts::Lr2021& radio, ports::Clock& clock, int, bus::AdsbQueue& out)
        : radio_(radio), clock_(clock), out_(out) {}

    Status begin() override {
        Status status = radio_.begin(parts::lr2021::kDetectionThresholdDbm);
        if (status == Status::Ok) status = radio_.start_receive();
        if (status != Status::Ok) {
            radio_.park();
            return status;
        }
        started_ = true;
        serviced_ms_ = clock_.milliseconds();
        return Status::Ok;
    }

    void service(uint32_t now_ms) {
        if (!started_ || stopped_) return;
        const uint64_t woke_us = clock_.microseconds();
        while (radio_.irq_asserted()) {
            events::AdsbFrame frame{};
            const parts::Lr2021Event event = radio_.poll(frame.chips.data(), frame.chips.size());
            if (event.type != parts::Lr2021EventType::Frame) break;
            frame.at_us = woke_us;
            frame.rssi_dbm = event.rssi_dbm;
            frame.rssi_valid = event.rssi_valid;
            (void)out_.push(frame);
            statistics_.heard(event.rssi_dbm, event.rssi_valid, woke_us);
        }
        (void)radio_.service(now_ms - serviced_ms_, runtime::kRadioRxSilenceCeilingMs);
        serviced_ms_ = now_ms;
    }

    ports::AdsbReceiverStatistics statistics() const override {
        ports::AdsbReceiverStatistics statistics = statistics_;
        statistics.roll(clock_.microseconds());
        statistics.rearms = radio_.rearms();
        statistics.resets = radio_.resets();
        statistics.command_errors = radio_.command_errors();
        statistics.device_errors = radio_.device_errors();
        statistics.dropped = radio_.fifo_drops() + out_.dropped();
        return statistics;
    }

    // INFO: fc 12nov26 a part never started is parked here: asleep if it takes it, held in reset if
    // not
    void sleep() override {
        if (stopped_) return;
        stopped_ = true;
        if (!started_) {
            radio_.park();
            parked_ = true;
            return;
        }
        parked_ = radio_.sleep() == Status::Ok;
    }

    bool parked() const override { return parked_; }

    void abandon() override {
        if (parked()) return;
        radio_.hold_in_reset();
        parked_ = true;
    }

   private:
    parts::Lr2021& radio_;
    ports::Clock& clock_;
    bus::AdsbQueue& out_;
    ports::AdsbReceiverStatistics statistics_{};
    uint32_t serviced_ms_{0};
    bool started_{false};
    bool stopped_{false};
    bool parked_{false};
};

}  // namespace skyblip::platform::host

#endif
