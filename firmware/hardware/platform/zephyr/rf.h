#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include <algorithm>
#include <limits>

#include "core/bus/bus.h"
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
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
    // INFO: fc 28sep26 read inside the dwell, where nine reads after its end ate into every guard
    static constexpr uint64_t kCarrierLeadUs = 2000;
    // INFO: fc 29sep26 staging's four commands took 0.25-0.7 ms on the 856 bench
    static constexpr uint64_t kTxStageLeadUs = 700;

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
        Status verdict = Status::Ok;
        k_sched_lock();
        if (joins_flying_dwell(plan)) {
            if (!flying_bursts_.add(burst_of(plan))) verdict = Status::WouldBlock;
        } else if (flying_ && plan.end_us <= flying_end_us_) {
            verdict = Status::WouldBlock;
        } else if (queued_ && same_dwell(plan, plan_)) {
            if (plan.tx != nullptr && !queued_bursts_.add(burst_of(plan)))
                verdict = Status::WouldBlock;
        } else {
            plan_ = plan;
            queued_bursts_ = ports::RfBursts::of(plan);
            plan_armed_at_us_ = clock_.micros();
            queued_ = true;
            k_sem_give(&armed_);
        }
        k_sched_unlock();
        return verdict;
    }

    void abort() override {
        k_sched_lock();
        queued_ = false;
        flying_ = false;
        k_sched_unlock();
        abort_ = true;
    }

    // The shutdown path runs on the service thread and the radio belongs to this
    // one, so what crosses the boundary is a request: abort the dwell, wake the
    // thread, and let it issue SetSleep itself. Nothing else may touch the SPI
    // while a dwell is on it.
    void sleep() override {
        k_sched_lock();
        queued_ = false;
        k_sched_unlock();
        sleep_requested_ = true;
        abort_ = true;
        k_sem_give(&armed_);
    }

    ports::RfCarrier carrier() const override { return carrier_; }

    ports::RfTransmitter transmitter() const override {
        return {parts::sx::kConductedDbm, parts::sx::kPaConfigHighPowerRatedDbm};
    }

    ports::RfSwitching switching() const override {
        k_sched_lock();
        const ports::RfSwitching switching = switching_;
        k_sched_unlock();
        return switching;
    }

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
        if (!flying_ || plan.tx == nullptr) return false;
        if (plan.mode != flying_mode_ || plan.freq_hz != flying_freq_) return false;
        return plan.tx_at_us >= clock_.micros() && plan.tx_at_us < flying_end_us_;
    }

    static ports::RfBurst burst_of(const ports::RfPlan& plan) {
        return ports::RfBurst{plan.tx, plan.tx_len, plan.tx_at_us};
    }

    static bool same_dwell(const ports::RfPlan& one, const ports::RfPlan& other) {
        return one.mode == other.mode && one.freq_hz == other.freq_hz && one.end_us == other.end_us;
    }

    ports::RfBursts flying_bursts() {
        k_sched_lock();
        const ports::RfBursts bursts = flying_bursts_;
        k_sched_unlock();
        return bursts;
    }

    void miss_unfinished(const ports::RfBursts& bursts, uint8_t done, uint8_t keyed) {
        for (uint8_t i = done; i < bursts.count; i++) {
            tx_at_us_ = bursts.burst[i].at_us;
            if (i >= keyed) keyed_at_us_ = 0;
            emit(events::RfEventType::Missed, clock_.micros());
        }
    }

    void run() {
        health_us_ = clock_.micros();
        for (;;) {
            const int armed = k_sem_take(&armed_, K_MSEC(kHealthTickMs));
            health(reinit_affordable());
            if (armed != 0) continue;
            if (sleep_requested_) {
                sleep_requested_ = false;
                radio_.sleep();
                continue;
            }
            abort_ = false;
            ports::RfPlan plan{};
            uint64_t armed_at_us = 0;
            if (!take(plan, armed_at_us)) continue;
            if (clock_.micros() >= plan.end_us) {
                miss_unfinished(flying_bursts(), 0, 0);
                flying_ = false;
                continue;
            }
            const uint64_t lead_us = static_cast<uint64_t>(timing::kSwitchLeadMs) * 1000;
            if (plan.start_us > lead_us) sleep_until(plan.start_us - lead_us);
            if (abort_) {
                flying_ = false;
                continue;
            }
            if (start(plan, armed_at_us))
                dwell(plan);
            else
                miss_unfinished(flying_bursts(), 0, 0);
            last_end_us_ = plan.end_us;
            flying_ = false;
            health(reinit_affordable());
        }
    }

    // INFO: fc 27sep26 flying from the moment it is taken, so a burst armed during the retune joins
    // it
    bool take(ports::RfPlan& plan, uint64_t& armed_at_us) {
        k_sched_lock();
        const bool taken = queued_;
        if (taken) {
            plan = plan_;
            armed_at_us = plan_armed_at_us_;
            queued_ = false;
            flying_bursts_ = queued_bursts_;
            flying_mode_ = plan.mode;
            flying_freq_ = plan.freq_hz;
            flying_end_us_ = plan.end_us;
            flying_ = true;
        }
        k_sched_unlock();
        return taken;
    }

    // A receiver that has heard nothing for 30 s is deaf, not lucky, and the
    // only cure is a reinitialisation. It is accumulated here, between dwells,
    // because this thread owns the bus: the elapsed time comes off the same
    // clock the deadlines do, so a dwell that overran is counted, not lost.
    void health(bool may_reinit) {
        const uint64_t now_us = clock_.micros();
        if (now_us <= health_us_) return;
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - health_us_) / 1000);
        if (elapsed_ms == 0) return;
        health_us_ += static_cast<uint64_t>(elapsed_ms) * 1000;
        const uint32_t rope_ms =
            may_reinit ? runtime::kRadioNoRxReinitMs : std::numeric_limits<uint32_t>::max();
        radio_.service(elapsed_ms, rope_ms);
    }

    // INFO: fc 28sep26 a no-RX reinit fits the bench's 23 ms stall, only the uplink dwell spares it
    bool reinit_affordable() {
        k_sched_lock();
        const bool affordable = !queued_ || plan_.mode == ports::RfMode::RxOband;
        k_sched_unlock();
        return affordable;
    }

    void sleep_until(uint64_t deadline_us) {
        const uint64_t now = clock_.micros();
        if (deadline_us <= now) return;
        const uint64_t left = deadline_us - now;
        if (left > 2000) k_usleep(static_cast<int32_t>(left - 1000));
        while (clock_.micros() < deadline_us) k_busy_wait(10);
    }

    // INFO: fc 23sep26 a radio half configured may sit on the last dwell's channel: it keys nothing
    bool start(const ports::RfPlan& plan, uint64_t armed_at_us) {
        const uint64_t from_us = clock_.micros();
        band_ = plan.mode == ports::RfMode::RxOband ? model::Band::O : model::Band::M;
        freq_hz_ = plan.freq_hz;
        if (radio_.wake() != Status::Ok) return false;
        if (plan.freq_hz != 0 && radio_.configure_radio(dwell_config(plan)) != Status::Ok)
            return false;
        if (radio_.mode() != parts::RadioMode::Rx && radio_.start_receive() != Status::Ok)
            return false;
        (void)radio_.wait_ready();
        ports::RfSwitch change{};
        change.from = last_mode_;
        change.to = plan.mode;
        change.from_hz = last_freq_hz_;
        change.to_hz = plan.freq_hz;
        change.previous_end_us = last_end_us_;
        change.began_us = from_us;
        change.ready_us = clock_.micros();
        change.start_us = plan.start_us;
        change.armed_ahead = armed_at_us < plan.start_us;
        switching_.note(change);
        last_mode_ = plan.mode;
        last_freq_hz_ = plan.freq_hz;
        return true;
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

    // INFO: fc 28sep26 staged ahead, so the instant costs one SetTx and not the whole buffer write
    void key(const ports::RfBurst& burst) {
        tx_at_us_ = burst.at_us;
        (void)radio_.stage_tx(burst.chips, burst.len);
        staged_at_us_ = clock_.micros();
        while (clock_.micros() < burst.at_us) k_busy_wait(1);
        (void)radio_.key_tx();
        keyed_at_us_ = clock_.micros();
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
        ports::RfBursts bursts{};
        uint8_t keyed = 0;
        uint8_t done = 0;
        bool completed = false;
        bool fault = false;
        bool sampled = false;
        keyed_at_us_ = 0;
        irq_at_us_ = 0;
        while (!abort_ && clock_.micros() < plan.end_us) {
            bursts = flying_bursts();
            if (clock_.micros() >= bursts.stage_at_us(keyed, done, kTxStageLeadUs)) {
                key(bursts.burst[keyed]);
                keyed++;
            }
            if (irq_at_us_ == 0 && radio_.irq_asserted()) irq_at_us_ = clock_.micros();
            completed = false;
            if (collect(completed, fault)) {
                if (completed) done++;
                continue;
            }
            if (fault) return;
            if (!sampled && keyed == done && keyed == bursts.count &&
                clock_.micros() + kCarrierLeadUs >= plan.end_us) {
                sample_carrier();
                sampled = true;
                continue;
            }
            // INFO: fc 29sep26 an absolute uptime wake: k_usleep lands a tick past the stage point
            const uint64_t wake_us = std::min(clock_.micros() + kSpinUs,
                                              bursts.stage_at_us(keyed, done, kTxStageLeadUs));
            k_sleep(K_TIMEOUT_ABS_TICKS(static_cast<k_ticks_t>(k_us_to_ticks_ceil64(wake_us))));
        }
        for (completed = false; collect(completed, fault); completed = false)
            if (completed) done++;
        if (fault) return;
        miss_unfinished(flying_bursts(), done, keyed);
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
        e.staged_at_us = staged_at_us_;
        e.tx_at_us = tx_at_us_;
        out_.push(e);
    }

    parts::Sx1262& radio_;
    ports::Clock& clock_;
    bus::Queue<events::RfEvent, 8>& out_;
    ports::RfPlan plan_{};
    ports::RfCarrier carrier_{};
    ports::RfSwitching switching_{};
    ports::RfMode last_mode_{ports::RfMode::Idle};
    uint32_t last_freq_hz_{0};
    uint64_t last_end_us_{0};
    uint64_t plan_armed_at_us_{0};
    events::RfEvent rx_{};
    model::Band band_{model::Band::M};
    uint32_t freq_hz_{0};
    uint64_t health_us_{0};
    struct k_sem armed_{};
    struct k_thread thread_{};
    k_tid_t tid_{nullptr};
    K_KERNEL_STACK_MEMBER(stack_, kStackSize);
    ports::RfBursts flying_bursts_{};
    ports::RfBursts queued_bursts_{};
    uint64_t keyed_at_us_{0};
    uint64_t staged_at_us_{0};
    uint64_t tx_at_us_{0};
    uint64_t irq_at_us_{0};
    uint64_t flying_end_us_{0};
    uint32_t flying_freq_{0};
    ports::RfMode flying_mode_{ports::RfMode::Idle};
    volatile bool flying_{false};
    bool queued_{false};
    volatile bool abort_{false};
    volatile bool sleep_requested_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
