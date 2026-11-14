#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ADSB_RECEIVER_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_ADSB_RECEIVER_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "core/events/adsb.h"
#include "hardware/parts/lr2021/lr2021.h"
#include "ports/adsb_receiver.h"
#include "ports/clock.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// INFO: fc 11nov26 the radio's own thread owns the SPI: it wakes on the IRQ edge and pushes each
// frame
class AdsbReceiver : public ports::AdsbReceiver {
   public:
    static constexpr int kStackBytes = 1536;
    static constexpr int kHealthTickMs = 250;
    // INFO: fc 11nov26 without the edge the FIFO's nine frames last 1 ms, so the thread polls well
    // inside it
    static constexpr int kFallbackPollUs = 200;

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
        statistics.dropped += out_.dropped();
        return statistics;
    }

    // TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
    // INFO: fc 13nov26 the thread owns the SPI, so it applies the tuning on its next wake
    void retune(const parts::Lr2021Tuning& tuning) {
        k_sched_lock();
        owed_tuning_ = tuning;
        retune_owed_ = true;
        k_sched_unlock();
        k_sem_give(&wake_);
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
            const k_timeout_t idle = irq_wakes_ ? K_MSEC(kHealthTickMs) : K_USEC(kFallbackPollUs);
            (void)k_sem_take(&wake_, idle);
            if (sleep_requested_) {
                park();
                return;
            }
            take_retune();
            drain();
            health();
            publish();
            listening_ = radio_.mode() == parts::Lr2021Mode::Receive;
        }
    }

    // TODO: fc 13nov26 adsb trial #293, remove once the trial picks the LR2021 settings
    void take_retune() {
        k_sched_lock();
        const bool owed = retune_owed_;
        const parts::Lr2021Tuning tuning = owed_tuning_;
        retune_owed_ = false;
        k_sched_unlock();
        if (owed) (void)radio_.retune(tuning);
    }

    // INFO: fc 11nov26 every frame of one wake is dated when the thread woke, the newest carries
    // the RSSI
    void drain() {
        const uint64_t woke_us = clock_.microseconds();
        for (;;) {
            events::AdsbFrame frame{};
            const parts::Lr2021Event event = radio_.poll(frame.chips.data(), frame.chips.size());
            if (event.type != parts::Lr2021EventType::Frame) return;
            frame.at_us = woke_us;
            frame.rssi_dbm = event.rssi_dbm;
            frame.rssi_valid = event.rssi_valid;
            (void)out_.push(frame);
            k_sched_lock();
            statistics_.heard(event.rssi_dbm, event.rssi_valid, woke_us);
            k_sched_unlock();
        }
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

    void publish() {
        k_sched_lock();
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
    uint64_t health_checked_us_{0};
    struct k_sem wake_{};
    struct gpio_callback irq_callback_{};
    volatile bool irq_wakes_{false};
    volatile bool sleep_requested_{false};
    volatile bool parked_{false};
    volatile bool stopped_{false};
    volatile bool listening_{false};
    parts::Lr2021Tuning owed_tuning_{};
    bool retune_owed_{false};
    struct k_thread thread_{};
    k_tid_t thread_id_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackBytes);
    static AdsbReceiver* self_;
};

inline AdsbReceiver* AdsbReceiver::self_ = nullptr;

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
