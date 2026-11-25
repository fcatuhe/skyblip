#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#if defined(__ZEPHYR__)

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <algorithm>
#include <limits>

#include "core/bus/bus.h"
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "hardware/platform/clear_channel.h"
#include "hardware/platform/zephyr/loop_wake.h"
#include "hardware/platform/zephyr/rf_mailbox.h"
#include "ports/clock.h"
#include "ports/rf.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// INFO: fc 20nov26 only this thread drives the radio, and it writes no flash, which blocks too long
class Rf : public ports::Rf {
   public:
    static constexpr int kStackBytes = 2048;
    static constexpr int kSpinUs = 200;
    // INFO: fc 03oct26 under a burst's 4.64 ms of air: a dead DIO1 still reads each frame in turn
    static constexpr uint64_t kSafetyPollUs = 4000;
    // INFO: fc 20nov26 short against the 30 s no-RX rope, long enough to let an idle device sleep
    static constexpr int kHealthTickMs = 250;
    // INFO: fc 28sep26 read inside the dwell, where nine reads after its end ate into every guard
    static constexpr uint64_t kCarrierLeadUs = 2000;
    // INFO: fc 03oct26 builds 875 and 892 dated TxDone 0.3-0.7 ms past the air time
    static constexpr uint64_t kTxTailUs = 1000;
    // INFO: fc 03oct26 two RTC ticks: at most 17 status reads a burst, none outside its tail
    static constexpr uint64_t kTxTailPollUs = 60;

    Rf(parts::Sx1262& radio, ports::Clock& clock, bus::RfQueue& out)
        : radio_(radio), clock_(clock), out_(out) {}

    Status begin() override {
        Status status = radio_.begin();
        if (status != Status::Ok) return status;
        status = radio_.configure_radio(parts::RadioConfig{});
        if (status != Status::Ok) return status;
        self_ = this;
        gpio_init_callback(&dio1_callback_, on_dio1, BIT(dio1_.pin));
        dio1_wakes_ = gpio_add_callback(dio1_.port, &dio1_callback_) == 0 && arm_dio1();
        thread_id_ = k_thread_create(
            &thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), entry, this, nullptr, nullptr,
            K_PRIO_COOP(static_cast<int>(runtime::TaskPriority::Rf)), 0, K_NO_WAIT);
        k_thread_name_set(thread_id_, "rf_exec");
        return Status::Ok;
    }

    Status arm(const ports::RfPlan& plan) override {
        if (sleep_requested_ || parked_) return Status::Down;
        if (plan.end_us <= plan.start_us) return Status::OutOfRange;
        if (plan.tx != nullptr && (plan.tx_at_us < plan.start_us || plan.tx_at_us >= plan.end_us))
            return Status::OutOfRange;
        if (clock_.microseconds() >= plan.end_us) return Status::WouldBlock;
        return mailbox_.arm(plan, clock_.microseconds());
    }

    void abort() override {
        mailbox_.abort();
        abort_ = true;
        mailbox_.wake();
    }

    Status withdraw(uint64_t tx_at_us) override { return mailbox_.withdraw(tx_at_us); }

    // INFO: fc 20nov26 the radio thread sends SetSleep, and no other uses the SPI before parked()
    void sleep() override {
        mailbox_.drop_queued();
        sleep_requested_ = true;
        abort_ = true;
        mailbox_.wake();
        mailbox_.give_armed();
    }

    // INFO: fc 08nov26 no thread means nothing ever owned the bus, so there is nothing to wait for
    bool parked() const override { return thread_id_ == nullptr || parked_; }

    // INFO: fc 08nov26 an abort mid-transfer keeps the SPI lock, so SYSTEM OFF checks abandoned()
    void abandon() override {
        if (parked()) return;
        k_thread_abort(thread_id_);
        (void)gpio_pin_interrupt_configure_dt(&dio1_, GPIO_INT_DISABLE);
        abandoned_ = true;
        parked_ = true;
    }

    static bool abandoned() { return abandoned_; }

    ports::RfCarrier carrier() const override { return carrier_; }

    ports::RfTransmitter transmitter() const override {
        return {parts::sx1262::kConductedDbm, parts::sx1262::kPaConfigHighPowerRatedDbm};
    }

    ports::RfDio1 dio1() const override { return {dio1_wakes_, dio1_edges_, dio1_missed_}; }

    ports::RfSwitching switching() const override {
        k_sched_lock();
        const ports::RfSwitching switching = switching_;
        k_sched_unlock();
        return switching;
    }

    // INFO: fc 20nov26 empty: run() checks the radio's health, so the SPI keeps one writer
    void service(uint32_t) {}

   private:
    static void entry(void* self, void*, void*) { static_cast<Rf*>(self)->run(); }

    static void on_dio1(const struct device*, struct gpio_callback*, gpio_port_pins_t) {
        self_->dio1_edges_++;
        self_->mailbox_.wake();
    }

    bool arm_dio1() { return gpio_pin_interrupt_configure_dt(&dio1_, GPIO_INT_EDGE_RISING) == 0; }

    void miss_unfinished(const ports::RfBursts& bursts, uint8_t done, uint8_t keyed) {
        for (uint8_t i = done; i < bursts.count; i++) {
            tx_at_us_ = bursts.burst[i].at_us;
            if (i >= keyed) keyed_at_us_ = 0;
            emit(events::RfEventType::Missed, clock_.microseconds());
        }
    }

    void run() {
        health_checked_us_ = clock_.microseconds();
        for (;;) {
            const int armed = mailbox_.wait_armed(K_MSEC(kHealthTickMs));
            if (sleep_requested_) {
                park();
                return;
            }
            health(mailbox_.reinitialisation_affordable());
            if (armed != 0) continue;
            abort_ = false;
            ports::RfPlan plan{};
            uint64_t armed_at_us = 0;
            if (!mailbox_.take(plan, armed_at_us)) continue;
            if (clock_.microseconds() >= plan.end_us) {
                miss_unfinished(mailbox_.flying_bursts(), 0, 0);
                mailbox_.land();
                continue;
            }
            const uint64_t retune_us = static_cast<uint64_t>(timing::kSwitchRetuneMs) * 1000;
            if (plan.start_us > retune_us) sleep_until(plan.start_us - retune_us);
            if (abort_) {
                mailbox_.land();
                continue;
            }
            if (start(plan, armed_at_us)) {
                dwell(plan);
            } else {
                switching_.refuse();
                miss_unfinished(mailbox_.flying_bursts(), 0, 0);
            }
            last_end_us_ = plan.end_us;
            mailbox_.land();
            health(mailbox_.reinitialisation_affordable());
        }
    }

    // INFO: fc 08nov26 the thread ends here, so no dwell, recovery or reinitialisation follows
    void park() {
        (void)gpio_pin_interrupt_configure_dt(&dio1_, GPIO_INT_DISABLE);
        radio_.sleep();
        parked_ = true;
    }

    // INFO: fc 20nov26 silence counts on the deadline clock, so a dwell that overran still counts
    void health(bool may_reinitialise) {
        const uint64_t now_us = clock_.microseconds();
        if (now_us <= health_checked_us_) return;
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - health_checked_us_) / 1000);
        if (elapsed_ms == 0) return;
        health_checked_us_ += static_cast<uint64_t>(elapsed_ms) * 1000;
        const uint32_t silence_ceiling_ms = may_reinitialise ? runtime::kRadioRxSilenceCeilingMs
                                                             : std::numeric_limits<uint32_t>::max();
        // INFO: fc 03oct26 a reinitialisation reconfigures DIO1, and nrfx GPIO drops its trigger
        if (radio_.service(elapsed_ms, silence_ceiling_ms)) dio1_wakes_ = dio1_wakes_ && arm_dio1();
    }

    // INFO: fc 19nov26 the clock counts ticks, so the tick at the deadline needs no spin after it
    void sleep_until(uint64_t deadline_us) {
        const k_timeout_t due =
            K_TIMEOUT_ABS_TICKS(static_cast<k_ticks_t>(k_us_to_ticks_ceil64(deadline_us)));
        while (!abort_ && clock_.microseconds() < deadline_us) mailbox_.wait_wake(due);
    }

    // INFO: fc 23sep26 a radio half configured may sit on the last dwell's channel: it keys nothing
    bool start(const ports::RfPlan& plan, uint64_t armed_at_us) {
        const uint64_t from_us = clock_.microseconds();
        band_ = plan.mode == ports::RfMode::RxOBand ? model::Band::O : model::Band::M;
        frequency_hz_ = plan.frequency_hz;
        if (radio_.wake() != Status::Ok) return false;
        if (plan.frequency_hz != 0 && radio_.configure_radio(dwell_config(plan)) != Status::Ok)
            return false;
        if (radio_.mode() != parts::RadioMode::Rx && radio_.start_receive() != Status::Ok)
            return false;
        (void)radio_.wait_ready();
        ports::RfSwitch change{};
        change.from = last_mode_;
        change.to = plan.mode;
        change.from_hz = last_frequency_hz_;
        change.to_hz = plan.frequency_hz;
        change.previous_end_us = last_end_us_;
        change.began_us = from_us;
        change.ready_us = clock_.microseconds();
        change.start_us = plan.start_us;
        change.armed_ahead = armed_at_us < plan.start_us;
        switching_.note(change);
        last_mode_ = plan.mode;
        last_frequency_hz_ = plan.frequency_hz;
        return true;
    }

    static parts::RadioConfig dwell_config(const ports::RfPlan& plan) {
        parts::RadioConfig config{};
        config.frequency_hz = plan.frequency_hz;
        if (plan.bitrate != 0) config.bitrate = plan.bitrate;
        if (plan.deviation_hz != 0) config.deviation_hz = plan.deviation_hz;
        if (plan.bandwidth_hz != 0) config.bandwidth_hz = plan.bandwidth_hz;
        config.gaussian_bt_e2 = plan.gaussian_bt_e2;
        config.frequency_correction_e1_ppm = plan.frequency_correction_e1_ppm;
        config.sync = plan.sync;
        config.sync_bits = plan.sync_bits;
        config.payload_bytes = plan.rx_bytes;
        config.tx_sync = plan.tx_sync;
        config.tx_sync_bits = plan.tx_sync_bits;
        config.tx_preamble_chips = plan.tx_preamble_chips;
        return config;
    }

    int8_t sample_carrier() {
        if (radio_.mode() != parts::RadioMode::Rx) return carrier_.level_dbm;
        carrier_.level_dbm = read_level();
        carrier_.samples++;
        return carrier_.level_dbm;
    }

    // INFO: fc 15sep26 the last read is kWindowUs after the first whatever a read costs on the bus
    int8_t read_level() {
        int8_t window[timing::ChannelLevel::kSamples];
        const uint64_t opened_us = clock_.microseconds();
        for (uint8_t i = 0; i < timing::ChannelLevel::kSamples; i++) {
            const uint64_t due_us =
                opened_us + static_cast<uint64_t>(i) * timing::ChannelLevel::kSampleSpacingUs;
            while (clock_.microseconds() < due_us) k_busy_wait(1);
            window[i] = radio_.instant_rssi_dbm();
        }
        return timing::ChannelLevel::mean_dbm(window, timing::ChannelLevel::kSamples);
    }

    // INFO: fc 14nov26 EN 300 220-1 5.21.1 before each burst not forced, kept to the dead time
    bool listen_and_key(const ports::RfBurst& burst, uint8_t index, bool& fault) {
        if (!mailbox_.claim(burst, index)) return false;
        tx_at_us_ = burst.at_us;
        staged_at_us_ = 0;
        if (burst.key_expired(clock_.microseconds())) return miss_unkeyed(index, fault);
        Assessment assessment{};
        if (!burst.forced) {
            assessment = assess(
                radio_.mode() == parts::RadioMode::Rx, [this] { return read_level(); }, clock_);
            if (burst.key_expired(assessment.ended_us)) return miss_unkeyed(index, fault);
            if (!assessment.clear) return refuse(burst, index, assessment);
        }
        if (radio_.stage_tx(burst.chips, burst.bytes) != Status::Ok ||
            radio_.wait_ready() != Status::Ok)
            return miss_unkeyed(index, fault);
        staged_at_us_ = clock_.microseconds();
        while (clock_.microseconds() < burst.at_us) k_busy_wait(1);
        const uint64_t now_us = clock_.microseconds();
        if (burst.key_expired(now_us)) return miss_unkeyed(index, fault);
        if (!burst.forced && !within_dead_time(assessment, now_us + kTxStartupAllowanceUs)) {
            fault = radio_.start_receive() != Status::Ok;
            if (fault) return miss_unkeyed(index, fault);
            return refuse(burst, index, Assessment{});
        }
        if (radio_.key_tx() != Status::Ok) return miss_unkeyed(index, fault);
        keyed_at_us_ = clock_.microseconds();
        tail_from_us_ = keyed_at_us_ + radio_.air_us(burst.bytes);
        return true;
    }

    bool refuse(const ports::RfBurst& burst, uint8_t index, const Assessment& assessment) {
        mailbox_.release(index);
        report(busy_report(burst, assessment, band_, frequency_hz_, clock_.microseconds()));
        return false;
    }

    bool miss_unkeyed(uint8_t index, bool& fault) {
        if (!fault && radio_.mode() != parts::RadioMode::Rx)
            fault = radio_.start_receive() != Status::Ok;
        mailbox_.release(index);
        keyed_at_us_ = 0;
        emit(events::RfEventType::Missed, clock_.microseconds());
        return false;
    }

    // INFO: fc 03oct26 build 892's TxDone edge woke 0.3 ms after the status bit, RxDone's on time
    uint64_t tail_poll_at(uint64_t now_us) const {
        if (now_us >= tail_from_us_ + kTxTailUs) return ports::RfBursts::kNever;
        return std::max(tail_from_us_, now_us + kTxTailPollUs);
    }

    // INFO: fc 16sep26 the event is dated where the radio raised it, not where the read-out ended
    bool collect(bool& completed, bool& fault) {
        const bool line_up = irq_at_us_ != 0 || radio_.irq_asserted();
        const uint64_t polled_us = irq_at_us_ != 0 ? irq_at_us_ : clock_.microseconds();
        irq_at_us_ = 0;
        const parts::RadioEvent event = radio_.poll(rx_.data.data(), events::kRfEventBytes);
        if (event.type != parts::RadioEventType::None && !line_up) dio1_missed_++;
        switch (event.type) {
            case parts::RadioEventType::None: return false;
            case parts::RadioEventType::RxDone: push_rx(event, polled_us); return true;
            case parts::RadioEventType::CrcError:
                emit(events::RfEventType::CrcError, polled_us, event);
                return true;
            case parts::RadioEventType::TxDone:
                completed = true;
                emit(events::RfEventType::TxDone, polled_us);
                (void)radio_.start_receive();
                return true;
            default:
                emit(events::RfEventType::Missed, polled_us);
                fault = true;
                return false;
        }
    }

    void dwell(const ports::RfPlan& plan) {
        ports::RfBursts bursts{};
        uint8_t keyed = 0;
        uint8_t done = 0;
        bool completed = false;
        bool fault = false;
        bool sampled = false;
        keyed_at_us_ = 0;
        irq_at_us_ = 0;
        uint64_t poll_due_us = 0;
        while (!abort_ && clock_.microseconds() < plan.end_us) {
            bursts = mailbox_.flying_bursts();
            if (clock_.microseconds() >= bursts.stage_at_us(keyed, done, kCcaLeadUs)) {
                if (!listen_and_key(bursts.burst[keyed], keyed, fault)) {
                    if (fault) {
                        miss_unfinished(mailbox_.flying_bursts(), done, keyed);
                        return;
                    }
                    continue;
                }
                keyed++;
            }
            if (irq_at_us_ == 0 && radio_.irq_asserted()) irq_at_us_ = clock_.microseconds();
            if (irq_at_us_ != 0 || clock_.microseconds() >= poll_due_us) {
                poll_due_us = clock_.microseconds() + kSafetyPollUs;
                completed = false;
                if (collect(completed, fault)) {
                    if (completed) done++;
                    continue;
                }
                if (fault) return;
            }
            const bool sent_all = keyed == done && keyed == bursts.count;
            if (!sampled && sent_all && clock_.microseconds() + kCarrierLeadUs >= plan.end_us) {
                sample_carrier();
                sampled = true;
                continue;
            }
            if (keyed > done)
                poll_due_us = std::min(poll_due_us, tail_poll_at(clock_.microseconds()));
            uint64_t wake_us =
                std::min({plan.end_us, poll_due_us, bursts.stage_at_us(keyed, done, kCcaLeadUs)});
            if (!dio1_wakes_) wake_us = std::min(wake_us, clock_.microseconds() + kSpinUs);
            if (!sampled && sent_all) wake_us = std::min(wake_us, plan.end_us - kCarrierLeadUs);
            // INFO: fc 29sep26 an absolute uptime wake: k_usleep lands a tick past the stage point
            mailbox_.wait_wake(
                K_TIMEOUT_ABS_TICKS(static_cast<k_ticks_t>(k_us_to_ticks_ceil64(wake_us))));
        }
        for (completed = false; collect(completed, fault); completed = false)
            if (completed) done++;
        if (fault) return;
        miss_unfinished(mailbox_.flying_bursts(), done, keyed);
    }

    // INFO: fc 20nov26 read into the event, since a 230-byte uplink would crowd the 2 KB stack
    void push_rx(const parts::RadioEvent& event, uint64_t at_us) {
        rx_.type = events::RfEventType::RxDone;
        rx_.band = band_;
        rx_.frequency_hz = frequency_hz_;
        rx_.bytes = event.bytes;
        rx_.rssi_dbm = event.rssi_dbm;
        rx_.rssi_valid = event.rssi_valid;
        rx_.at_us = at_us;
        report(rx_);
    }

    void emit(events::RfEventType type, uint64_t at_us, const parts::RadioEvent& event = {}) {
        events::RfEvent reported{};
        reported.type = type;
        reported.band = band_;
        reported.frequency_hz = frequency_hz_;
        reported.rssi_dbm = event.rssi_dbm;
        reported.rssi_valid = event.rssi_valid;
        reported.at_us = at_us;
        reported.keyed_at_us = keyed_at_us_;
        reported.staged_at_us = staged_at_us_;
        reported.tx_at_us = tx_at_us_;
        report(reported);
    }

    void report(const events::RfEvent& event) {
        out_.push(event);
        g_loop_wake.wake();
    }

    parts::Sx1262& radio_;
    ports::Clock& clock_;
    bus::RfQueue& out_;
    RfMailbox mailbox_{};
    ports::RfCarrier carrier_{};
    ports::RfSwitching switching_{};
    ports::RfMode last_mode_{ports::RfMode::Idle};
    uint32_t last_frequency_hz_{0};
    uint64_t last_end_us_{0};
    events::RfEvent rx_{};
    model::Band band_{model::Band::M};
    uint32_t frequency_hz_{0};
    uint64_t health_checked_us_{0};
    struct gpio_dt_spec dio1_ = GPIO_DT_SPEC_GET(DT_NODELABEL(radio_dio1_gpio), gpios);
    struct gpio_callback dio1_callback_{};
    volatile bool dio1_wakes_{false};
    volatile uint32_t dio1_edges_{0};
    uint32_t dio1_missed_{0};
    struct k_thread thread_{};
    k_tid_t thread_id_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackBytes);
    uint64_t keyed_at_us_{0};
    uint64_t staged_at_us_{0};
    uint64_t tx_at_us_{0};
    uint64_t irq_at_us_{0};
    uint64_t tail_from_us_{0};
    volatile bool abort_{false};
    volatile bool sleep_requested_{false};
    volatile bool parked_{false};
    static Rf* self_;
    static volatile bool abandoned_;
};

inline Rf* Rf::self_ = nullptr;
inline volatile bool Rf::abandoned_ = false;

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
