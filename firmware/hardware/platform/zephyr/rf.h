#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include "core/bus/bus.h"
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "ports/clock.h"
#include "ports/rf.h"
#include "runtime/tasks.h"

namespace skyblip::platform::zephyr {

// The radio executor on silicon: its own thread, above every other application
// thread, waking on an armed absolute deadline. Nothing on this thread writes
// flash or touches the BLE stack: a deferred internal-flash write blocks for
// milliseconds, which is more than the whole guard budget.
class Rf : public ports::Rf {
   public:
    static constexpr int kStackSize = 2048;
    static constexpr int kSpinUs = 200;
    // How long the thread waits for the next dwell before it looks at the
    // radio's health instead. Short against the 30 s no-RX rope, long enough
    // that an idle device is not woken for nothing.
    static constexpr int kHealthTickMs = 250;

    Rf(parts::Sx1262& radio, ports::Clock& clock, bus::Queue<events::RfEvent, 8>& out)
        : radio_(radio), clock_(clock), out_(out) {
        k_sem_init(&armed_, 0, 1);
    }

    Status begin() override {
        Status s = radio_.begin();
        if (s != Status::Ok) return s;
        s = radio_.configure_radio(parts::RadioConfig{});
        if (s != Status::Ok) return s;
        tid_ = k_thread_create(&thread_, stack_, K_THREAD_STACK_SIZEOF(stack_), entry, this,
                               nullptr, nullptr,
                               K_PRIO_COOP(static_cast<int>(runtime::TaskPrio::Rf)), 0, K_NO_WAIT);
        k_thread_name_set(tid_, "rf_exec");
        return Status::Ok;
    }

    Status arm(const ports::RfPlan& plan) override {
        if (plan.end_us <= plan.start_us) return Status::OutOfRange;
        if (plan.tx != nullptr && (plan.tx_at_us < plan.start_us || plan.tx_at_us >= plan.end_us))
            return Status::OutOfRange;
        // A dwell that cannot start before its own end is refused here rather
        // than truncated on air.
        if (clock_.micros() >= plan.end_us) return Status::WouldBlock;
        k_sched_lock();
        if (!joins_flying_dwell(plan)) {
            plan_ = plan;
            k_sem_give(&armed_);
        }
        k_sched_unlock();
        return Status::Ok;
    }

    void abort() override { abort_ = true; }

    // The shutdown path runs on the service thread and the radio belongs to this
    // one, so what crosses the boundary is a request: abort the dwell, wake the
    // thread, and let it issue SetSleep itself. Nothing else may touch the SPI
    // while a dwell is on it.
    void sleep() override {
        sleep_requested_ = true;
        abort_ = true;
        k_sem_give(&armed_);
    }

    ports::RfCarrier carrier() const override { return carrier_; }

    // The board calls this from the service pass, and there is deliberately
    // nothing here: the radio belongs to the thread below, and reinitialising it
    // from the service list would put a second writer on the SPI bus while a
    // dwell is using it. The health watchdog runs in run(), where the chip is
    // owned.
    void service(uint32_t) {}

   private:
    static void entry(void* self, void*, void*) { static_cast<Rf*>(self)->run(); }

    // INFO: fc 23sep26 runs under arm()'s scheduler lock: no dwell ends between check and publish
    bool joins_flying_dwell(const ports::RfPlan& plan) {
        if (!flying_ || plan.tx == nullptr || burst_ != nullptr) return false;
        if (plan.mode != flying_mode_ || plan.freq_hz != flying_freq_) return false;
        if (plan.tx_at_us < clock_.micros() || plan.tx_at_us >= flying_end_us_) return false;
        burst_at_us_ = plan.tx_at_us;
        burst_len_ = plan.tx_len;
        burst_ = plan.tx;
        return true;
    }

    void run() {
        health_us_ = clock_.micros();
        for (;;) {
            const int armed = k_sem_take(&armed_, K_MSEC(kHealthTickMs));
            health();
            if (armed != 0) continue;
            if (sleep_requested_) {
                sleep_requested_ = false;
                radio_.sleep();
                continue;
            }
            abort_ = false;
            const ports::RfPlan plan = plan_;
            if (clock_.micros() >= plan.end_us) {
                if (plan.tx != nullptr) emit(events::RfEventType::Missed, clock_.micros());
                continue;
            }
            sleep_until(plan.start_us);
            if (abort_) continue;
            burst_ = nullptr;
            flying_mode_ = plan.mode;
            flying_freq_ = plan.freq_hz;
            flying_end_us_ = plan.end_us;
            flying_ = true;
            if (start(plan))
                dwell(plan);
            else if (plan.tx != nullptr)
                emit(events::RfEventType::Missed, clock_.micros());
            flying_ = false;
            health();
        }
    }

    // A receiver that has heard nothing for 30 s is deaf, not lucky, and the
    // only cure is a reinitialisation. It is accumulated here, between dwells,
    // because this thread owns the bus: the elapsed time comes off the same
    // clock the deadlines do, so a dwell that overran is counted, not lost.
    void health() {
        const uint64_t now_us = clock_.micros();
        if (now_us <= health_us_) return;
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - health_us_) / 1000);
        if (elapsed_ms == 0) return;
        health_us_ += static_cast<uint64_t>(elapsed_ms) * 1000;
        radio_.service(elapsed_ms, runtime::kRadioNoRxReinitMs);
    }

    void sleep_until(uint64_t deadline_us) {
        const uint64_t now = clock_.micros();
        if (deadline_us <= now) return;
        const uint64_t left = deadline_us - now;
        if (left > 2000) k_usleep(static_cast<int32_t>(left - 1000));
        while (clock_.micros() < deadline_us) k_busy_wait(10);
    }

    // INFO: fc 23sep26 a radio half configured may sit on the last dwell's channel: it keys nothing
    bool start(const ports::RfPlan& plan) {
        band_ = plan.mode == ports::RfMode::RxOband ? model::Band::O : model::Band::M;
        freq_hz_ = plan.freq_hz;
        if (radio_.wake() != Status::Ok) return false;
        if (plan.freq_hz != 0 && radio_.configure_radio(dwell_config(plan)) != Status::Ok)
            return false;
        return radio_.start_receive() == Status::Ok;
    }

    // The whole modem, not just the synthesiser: the two bands are two
    // modulations (ADS-L 4 SRD-860 issue 2 §C.2 against §C.4) and the plan
    // carries both halves.
    static parts::RadioConfig dwell_config(const ports::RfPlan& plan) {
        parts::RadioConfig cfg{};
        cfg.freq_hz = plan.freq_hz;
        if (plan.bitrate != 0) cfg.bitrate = plan.bitrate;
        if (plan.fdev_hz != 0) cfg.fdev_hz = plan.fdev_hz;
        if (plan.bandwidth_hz != 0) cfg.bandwidth_hz = plan.bandwidth_hz;
        cfg.gaussian_bt_e2 = plan.gaussian_bt_e2;
        cfg.freq_corr_e1_ppm = plan.freq_corr_e1_ppm;
        cfg.sync = plan.sync;
        cfg.sync_bits = plan.sync_bits;
        cfg.payload_bytes = plan.rx_len;
        return cfg;
    }

    // INFO: fc 15sep26 the last read is kWindowUs after the first whatever a read costs on the bus
    int8_t sample_carrier() {
        if (radio_.mode() != parts::RadioMode::Rx) return carrier_.dbm;
        int8_t window[timing::ChannelLevel::kSamples];
        const uint64_t opened_us = clock_.micros();
        for (uint8_t i = 0; i < timing::ChannelLevel::kSamples; i++) {
            const uint64_t due_us =
                opened_us + static_cast<uint64_t>(i) * timing::ChannelLevel::kSampleSpacingUs;
            while (clock_.micros() < due_us) k_busy_wait(1);
            window[i] = radio_.rssi_inst();
        }
        carrier_.dbm = timing::ChannelLevel::mean_dbm(window, timing::ChannelLevel::kSamples);
        carrier_.samples++;
        return carrier_.dbm;
    }

    // INFO: fc 16sep26 the event is dated where the radio raised it, not where the read-out ended
    bool collect(bool& completed, bool& fault) {
        const uint64_t polled_us = irq_at_us_ != 0 ? irq_at_us_ : clock_.micros();
        irq_at_us_ = 0;
        const parts::RadioEvent ev = radio_.poll(rx_.data.data(), events::kRfEventBytes);
        switch (ev.type) {
            case parts::RadioEventType::None: return false;
            case parts::RadioEventType::RxDone: push_rx(ev, polled_us); return true;
            case parts::RadioEventType::CrcError:
                emit(events::RfEventType::CrcError, polled_us, ev);
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
        const uint8_t* tx = plan.tx;
        uint8_t tx_len = plan.tx_len;
        uint64_t tx_at_us = plan.tx_at_us;
        bool completed = false;
        bool transmitted = false;
        bool fault = false;
        keyed_at_us_ = 0;
        irq_at_us_ = 0;
        while (!abort_ && clock_.micros() < plan.end_us) {
            if (tx == nullptr && burst_ != nullptr) {
                k_sched_lock();
                tx = const_cast<const uint8_t*>(burst_);
                tx_len = burst_len_;
                tx_at_us = burst_at_us_;
                k_sched_unlock();
            }
            if (tx != nullptr && !transmitted && clock_.micros() >= tx_at_us) {
                transmitted = true;
                (void)radio_.transmit(tx, tx_len);
                keyed_at_us_ = clock_.micros();
            }
            if (irq_at_us_ == 0 && radio_.irq_asserted()) irq_at_us_ = clock_.micros();
            if (collect(completed, fault)) continue;
            if (fault) return;
            k_usleep(kSpinUs);
        }
        while (collect(completed, fault)) {
        }
        if (fault) return;
        sample_carrier();
        if (tx != nullptr && !completed) emit(events::RfEventType::Missed, clock_.micros());
    }

    // The frame is already in the event that will carry it. An O-band uplink
    // codeword is 255 bytes, and staging one on this thread's 2 KB stack as well
    // as on the queue would be the same bytes twice. The band the dwell was
    // armed for travels with it: the O band carries one system and the M band
    // two, and only the arming knows which of them this burst is.
    void push_rx(const parts::RadioEvent& ev, uint64_t at_us) {
        rx_.type = events::RfEventType::RxDone;
        rx_.band = band_;
        rx_.freq_hz = freq_hz_;
        rx_.len = ev.len;
        rx_.rssi_dbm = ev.rssi_dbm;
        rx_.rssi_valid = ev.rssi_valid;
        rx_.at_us = at_us;
        out_.push(rx_);
    }

    void emit(events::RfEventType type, uint64_t at_us, const parts::RadioEvent& ev = {}) {
        events::RfEvent e{};
        e.type = type;
        e.band = band_;
        e.freq_hz = freq_hz_;
        e.rssi_dbm = ev.rssi_dbm;
        e.rssi_valid = ev.rssi_valid;
        e.at_us = at_us;
        e.keyed_at_us = keyed_at_us_;
        out_.push(e);
    }

    parts::Sx1262& radio_;
    ports::Clock& clock_;
    bus::Queue<events::RfEvent, 8>& out_;
    ports::RfPlan plan_{};
    ports::RfCarrier carrier_{};
    events::RfEvent rx_{};
    model::Band band_{model::Band::M};
    uint32_t freq_hz_{0};
    uint64_t health_us_{0};
    struct k_sem armed_{};
    struct k_thread thread_{};
    k_tid_t tid_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackSize);
    const uint8_t* volatile burst_{nullptr};
    uint64_t burst_at_us_{0};
    uint64_t keyed_at_us_{0};
    uint64_t irq_at_us_{0};
    uint64_t flying_end_us_{0};
    uint32_t flying_freq_{0};
    ports::RfMode flying_mode_{ports::RfMode::Idle};
    uint8_t burst_len_{0};
    volatile bool flying_{false};
    volatile bool abort_{false};
    volatile bool sleep_requested_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
