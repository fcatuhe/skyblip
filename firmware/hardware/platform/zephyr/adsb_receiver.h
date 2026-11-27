#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ADSB_RECEIVER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ADSB_RECEIVER_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <iterator>

#include "core/bus/bus.h"
#include "core/events/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "hardware/platform/adsb_batch.h"
#include "ports/adsb_receiver.h"
#include "ports/clock.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// INFO: fc 11nov26 the radio's own thread owns the SPI: it wakes on the IRQ edge and pushes the
// frames
class AdsbReceiver : public ports::AdsbReceiver {
   public:
    static constexpr int kStackBytes = 1536;
    // INFO: fc 27nov26 session 13 spent 1.18 ms a wake, so a quiet sky is swept, not woken per
    // frame
    static constexpr uint32_t kSweepPeriodMs = 20;
    static constexpr uint32_t kFallbackSweepPeriodMs = 5;
    // INFO: fc 27nov26 the most frames a second bravo-d582 heard outdoors, 1460 in session 13
    static constexpr uint32_t kFloodFramesPerSecond = 1500;
    // INFO: fc 27nov26 session 13's longest wake was 2.84 ms, its once-a-second probe included
    static constexpr uint32_t kWakeAllowanceUs = 3000;
    static_assert((parts::lr2021::kFifoFrames - parts::lr2021::kFifoHighFrames) * 1000000ULL >=
                      1ULL * kFloodFramesPerSecond * kWakeAllowanceUs,
                  "the FIFO past its IRQ level holds a flood's frames for one wake");
    static_assert(parts::lr2021::kFifoFrames * 1000ULL >=
                      2ULL * kFloodFramesPerSecond * kFallbackSweepPeriodMs,
                  "with no IRQ edge, the FIFO holds twice a flood's frames for one sweep");
    static constexpr uint64_t kProbePeriodUs = 1000000;

    AdsbReceiver(parts::Lr2021& radio, ports::Clock& clock, const struct gpio_dt_spec& irq,
                 bus::AdsbQueue& out,
                 int16_t detection_threshold_dbm = parts::lr2021::kDetectionThresholdDbm)
        : radio_(radio),
          clock_(clock),
          irq_(irq),
          out_(out),
          detection_threshold_dbm_(detection_threshold_dbm) {
        k_sem_init(&wake_, 0, 1);
    }

    Status begin() override {
        Status status = radio_.begin(detection_threshold_dbm_);
        if (status == Status::Ok) status = radio_.start_receive();
        // INFO: fc 11nov26 no thread will ever park a part that failed here, so it is parked now
        if (status != Status::Ok) {
            radio_.park();
            return status;
        }
        self_ = this;
        gpio_init_callback(&irq_callback_, on_irq, BIT(irq_.pin));
        irq_wakes_ = gpio_add_callback(irq_.port, &irq_callback_) == 0 && arm_irq();
        listening_ = true;
        health_checked_us_ = clock_.microseconds();
        probed_us_ = health_checked_us_;
        thread_id_ = k_thread_create(
            &thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), entry, this, nullptr, nullptr,
            K_PRIO_COOP(static_cast<int>(runtime::TaskPriority::Rf)), 0, K_NO_WAIT);
        k_thread_name_set(thread_id_, "adsb_rx");
        return Status::Ok;
    }

    // INFO: fc 12nov26 the thread drains the FIFO on its own, so the board's pass has nothing to do
    void service(uint32_t) {}

    ports::AdsbReceiverStatistics statistics() const override {
        k_sched_lock();
        ports::AdsbReceiverStatistics statistics = statistics_;
        k_sched_unlock();
        statistics.roll(clock_.microseconds());
        statistics.queue_drops = out_.dropped();
        statistics.dropped += statistics.queue_drops;
        statistics.irq_armed = irq_wakes_;
        return statistics;
    }

    // INFO: fc 12nov26 a part no thread runs, never probed or never started, is parked from here
    void sleep() override {
        if (thread_id_ == nullptr) {
            if (!parked_) radio_.park();
            parked_ = true;
            return;
        }
        sleep_requested_ = true;
        k_sem_give(&wake_);
    }

    bool parked() const override { return parked_; }

    bool listening() const override { return listening_; }

    // INFO: fc 11nov26 GPIO only: a thread aborted inside a transfer keeps the SPI lock, so no SPI
    // follows
    void abandon() override {
        if (parked()) return;
        if (thread_id_ != nullptr && !stopped_) k_thread_abort(thread_id_);
        listening_ = false;
        (void)gpio_pin_interrupt_configure_dt(&irq_, GPIO_INT_DISABLE);
        radio_.hold_in_reset();
        parked_ = true;
    }

   private:
    static void entry(void* self, void*, void*) { static_cast<AdsbReceiver*>(self)->run(); }

    static void on_irq(const struct device*, struct gpio_callback*, gpio_port_pins_t) {
        k_sem_give(&self_->wake_);
    }

    bool arm_irq() { return gpio_pin_interrupt_configure_dt(&irq_, GPIO_INT_EDGE_RISING) == 0; }

    void run() {
        for (;;) {
            const k_timeout_t sweep = K_MSEC(irq_wakes_ ? kSweepPeriodMs : kFallbackSweepPeriodMs);
            const bool edge = k_sem_take(&wake_, sweep) == 0;
            if (sleep_requested_) {
                park();
                return;
            }
            const uint32_t started = k_cycle_get_32();
            const uint32_t drained = drain();
            if (edge) edge_wakes_++;
            if (drained == 0) empty_wakes_++;
            // INFO: fc 27nov26 a sweep finding the FIFO past its IRQ level means an edge was lost
            if (!edge && irq_wakes_ && drained >= parts::lr2021::kFifoHighFrames) missed_edges_++;
            health();
            alternate_settings();
            probe();
            const uint64_t spent_us = k_cyc_to_us_floor64(k_cycle_get_32() - started);
            drain_busy_us_ += spent_us;
            drains_++;
            k_sched_lock();
            statistics_.drained(drained, out_.size(), static_cast<uint32_t>(spent_us),
                                clock_.microseconds());
            k_sched_unlock();
            publish();
            listening_ = radio_.mode() == parts::Lr2021Mode::Receive;
        }
    }

    // INFO: fc 27nov26 a frame landing during the read waits for the next wake: one snapshot a wake
    uint32_t drain() {
        const uint64_t woke_us = clock_.microseconds();
        const uint32_t polled = k_cycle_get_32();
        const parts::Lr2021Event event = radio_.poll(batch_, sizeof(batch_));
        const uint32_t polled_until = k_cycle_get_32();
        poll_us_ += k_cyc_to_us_floor64(polled_until - polled);
        if (event.type != parts::Lr2021EventType::Frame) return 0;
        k_sched_lock();
        hand_over(batch_, event, woke_us, statistics_, out_);
        k_sched_unlock();
        return event.frames;
    }

    // INFO: fc 11nov26 a reset reconfigures the IRQ pin, and nrfx GPIO drops its trigger with it
    void health() {
        const uint64_t now_us = clock_.microseconds();
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - health_checked_us_) / 1000);
        if (elapsed_ms == 0) return;
        health_checked_us_ += static_cast<uint64_t>(elapsed_ms) * 1000;
        if (radio_.service(elapsed_ms, runtime::kRadioRxSilenceCeilingMs))
            irq_wakes_ = irq_wakes_ && arm_irq();
    }

    struct Setting {
        int16_t threshold_dbm;
        uint8_t rx_boost;
        uint8_t gain_step;
    };

    // TODO: fc 27nov26 a bench-only A/B of threshold, boost and gain, removed once a walk picks
    // them
    static constexpr Setting kSettings[] = {
        {-110, 0, 13}, {-110, 7, 13}, {-106, 0, 13}, {-106, 7, 13}, {-110, 0, 7}};

    void alternate_settings() {
        const int64_t minute = k_uptime_get() / 60000;
        const size_t index = static_cast<size_t>(minute % std::size(kSettings));
        if (index == setting_index_) return;
        setting_index_ = index;
        const Setting& setting = kSettings[index];
        (void)radio_.retune(setting.threshold_dbm, setting.rx_boost, setting.gain_step);
    }

    void probe() {
        const uint64_t now_us = clock_.microseconds();
        if (now_us - probed_us_ < kProbePeriodUs) return;
        probed_us_ = now_us;
        if (radio_.sample(probe_) != Status::Ok) probe_.valid = false;
    }

    void publish() {
        k_sched_lock();
        statistics_.missed_edges = missed_edges_;
        statistics_.part_resets = radio_.part_resets();
        statistics_.framing_resyncs = radio_.framing_resyncs();
        statistics_.drains = drains_;
        statistics_.drain_busy_us = drain_busy_us_;
        statistics_.irq_wakes = edge_wakes_;
        statistics_.empty_wakes = empty_wakes_;
        statistics_.poll_us = poll_us_;
        statistics_.busy_wait_us = radio_.busy_wait_us();
        statistics_.transactions = radio_.transactions();
        statistics_.overflow_drops = radio_.overflow_drops();
        statistics_.resync_drops = radio_.resync_drops();
        statistics_.in_flight = radio_.in_flight();
        statistics_.split_counts = radio_.split_counts();
        statistics_.stranded_resyncs = radio_.stranded_resyncs();
        statistics_.short_resyncs = radio_.short_resyncs();
        statistics_.probe = probe_;
        statistics_.rearms = radio_.rearms();
        statistics_.resets = radio_.resets();
        statistics_.command_errors = radio_.command_errors();
        statistics_.device_errors = radio_.device_errors();
        statistics_.dropped = radio_.fifo_drops();
        k_sched_unlock();
    }

    // INFO: fc 11nov26 a sleep the part never confirmed leaves parked() false, for the shutdown to
    // abandon()
    void park() {
        (void)gpio_pin_interrupt_configure_dt(&irq_, GPIO_INT_DISABLE);
        listening_ = false;
        parked_ = radio_.sleep() == Status::Ok;
        stopped_ = true;
    }

    parts::Lr2021& radio_;
    ports::Clock& clock_;
    struct gpio_dt_spec irq_;
    bus::AdsbQueue& out_;
    int16_t detection_threshold_dbm_;
    ports::AdsbReceiverStatistics statistics_{};
    ports::ReceiverProbe probe_{};
    uint64_t health_checked_us_{0};
    uint64_t probed_us_{0};
    uint32_t missed_edges_{0};
    uint32_t drains_{0};
    uint64_t drain_busy_us_{0};
    uint32_t edge_wakes_{0};
    uint32_t empty_wakes_{0};
    uint64_t poll_us_{0};
    size_t setting_index_{0};
    uint8_t batch_[kAdsbBatchBytes]{};
    struct k_sem wake_{};
    struct gpio_callback irq_callback_{};
    volatile bool irq_wakes_{false};
    volatile bool sleep_requested_{false};
    volatile bool parked_{false};
    volatile bool stopped_{false};
    volatile bool listening_{false};
    struct k_thread thread_{};
    k_tid_t thread_id_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackBytes);
    static AdsbReceiver* self_;
};

inline AdsbReceiver* AdsbReceiver::self_ = nullptr;

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
