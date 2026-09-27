#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_RF_H

#include "core/bus/bus.h"
#include "core/events/rf.h"
#include "core/model/band.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "hardware/parts/sx1262/sx1262.h"
#include "ports/clock.h"
#include "ports/rf.h"
#include "runtime/tasks.h"

namespace skyblip::platform::host {

// The radio executor on virtual time: the same arm/abort contract the silicon
// executor implements, driven by whatever clock the caller advances. Slot
// deadlines are therefore testable to the microsecond with no hardware.
class Rf : public ports::Rf {
   public:
    Rf(parts::Sx1262& radio, ports::Clock& clock, bus::Queue<events::RfEvent, 8>& out)
        : radio_(radio), clock_(clock), out_(out) {}

    Status begin() override {
        Status s = radio_.begin();
        if (s != Status::Ok) return s;
        return radio_.configure_radio(parts::RadioConfig{});
    }

    Status arm(const ports::RfPlan& plan) override {
        if (plan.end_us <= plan.start_us) return Status::OutOfRange;
        if (plan.tx != nullptr && (plan.tx_at_us < plan.start_us || plan.tx_at_us >= plan.end_us))
            return Status::OutOfRange;
        if (armed_ && joins_flying_dwell(plan))
            return bursts_.add(burst_of(plan)) ? Status::Ok : Status::WouldBlock;
        if (armed_ && plan.end_us <= plan_.end_us) return Status::WouldBlock;
        if (armed_ && has_pending_ && same_dwell(plan, pending_))
            return plan.tx == nullptr || pending_bursts_.add(burst_of(plan)) ? Status::Ok
                                                                             : Status::WouldBlock;
        if (armed_) {
            pending_ = plan;
            pending_bursts_ = ports::RfBursts::of(plan);
            pending_armed_at_us_ = clock_.micros();
            has_pending_ = true;
            return Status::Ok;
        }
        adopt(plan, ports::RfBursts::of(plan), clock_.micros());
        return Status::Ok;
    }

    void abort() override {
        armed_ = false;
        has_pending_ = false;
    }

    void sleep() override {
        armed_ = false;
        sleeps_++;
        radio_.sleep();
    }

    int sleeps() const { return sleeps_; }

    ports::RfCarrier carrier() const override { return carrier_; }

    ports::RfTransmitter transmitter() const override {
        return {parts::sx::kConductedDbm, parts::sx::kPaConfigHighPowerRatedDbm};
    }

    ports::RfSwitching switching() const override { return switching_; }

    void service(uint32_t now_ms) {
        const uint64_t now_us = clock_.micros();
        const uint32_t dt = now_ms - last_ms_;
        last_ms_ = now_ms;
        radio_.service(dt, runtime::kRadioNoRxReinitMs);

        fly(now_us);
        // The receiver keeps reporting between dwells: a frame that arrived
        // while the next plan was being armed is in the chip, not lost.
        if (started_ || radio_.mode() == parts::RadioMode::Rx) drain(now_us);
        if (armed_ && now_us >= plan_.end_us) finish(now_us);
        if (!armed_ && has_pending_) {
            take_pending(now_us);
            fly(now_us);
        }
    }

    uint32_t armed_count() const { return armed_count_; }

   private:
    static ports::RfBurst burst_of(const ports::RfPlan& plan) {
        return ports::RfBurst{plan.tx, plan.tx_len, plan.tx_at_us};
    }

    static bool same_dwell(const ports::RfPlan& one, const ports::RfPlan& other) {
        return one.mode == other.mode && one.freq_hz == other.freq_hz && one.end_us == other.end_us;
    }

    bool joins_flying_dwell(const ports::RfPlan& plan) const {
        return plan.tx != nullptr && plan.mode == plan_.mode && plan.freq_hz == plan_.freq_hz &&
               plan.tx_at_us >= clock_.micros() && plan.tx_at_us < plan_.end_us;
    }

    bool keying() const { return keyed_ > done_; }

    void finish(uint64_t now_us) {
        sample_carrier();
        miss_unfinished(bursts_, done_, keyed_, now_us);
        armed_ = false;
        started_ = false;
    }

    void fly(uint64_t now_us) {
        const uint64_t lead_us = static_cast<uint64_t>(timing::kSwitchLeadMs) * 1000;
        if (armed_ && !started_ && now_us + lead_us >= plan_.start_us && !start(now_us))
            abandon(now_us);
        if (armed_ && started_ && !keying() && keyed_ < bursts_.count &&
            now_us >= bursts_.burst[keyed_].at_us)
            transmit(bursts_.burst[keyed_]);
    }

    void take_pending(uint64_t now_us) {
        has_pending_ = false;
        if (now_us >= pending_.end_us) {
            miss_unfinished(pending_bursts_, 0, 0, now_us);
            return;
        }
        adopt(pending_, pending_bursts_, pending_armed_at_us_);
    }

    void adopt(const ports::RfPlan& plan, const ports::RfBursts& bursts, uint64_t armed_at_us) {
        plan_ = plan;
        bursts_ = bursts;
        armed_at_us_ = armed_at_us;
        armed_ = true;
        started_ = false;
        keyed_ = 0;
        done_ = 0;
        keyed_at_us_ = 0;
    }

    void miss_unfinished(const ports::RfBursts& bursts, uint8_t done, uint8_t keyed,
                         uint64_t now_us) {
        for (uint8_t i = done; i < bursts.count; i++) {
            tx_at_us_ = bursts.burst[i].at_us;
            if (i >= keyed) keyed_at_us_ = 0;
            emit(events::RfEventType::Missed, now_us);
        }
    }

    // INFO: fc 23sep26 a radio half configured may sit on the last dwell's channel: it keys nothing
    bool start(uint64_t now_us) {
        started_ = true;
        armed_count_++;
        band_ = plan_.mode == ports::RfMode::RxOband ? model::Band::O : model::Band::M;
        freq_hz_ = plan_.freq_hz;
        if (radio_.wake() != Status::Ok) return false;
        if (plan_.freq_hz != 0 && radio_.configure_radio(dwell_config(plan_)) != Status::Ok)
            return false;
        if (radio_.start_receive() != Status::Ok) return false;
        const uint64_t ready_us = clock_.micros();
        const bool armed_ahead = armed_at_us_ < plan_.start_us;
        switching_.note(last_mode_, last_freq_hz_, plan_.mode, plan_.freq_hz,
                        static_cast<uint32_t>(ready_us - now_us),
                        armed_ahead && ready_us > plan_.start_us);
        last_mode_ = plan_.mode;
        last_freq_hz_ = plan_.freq_hz;
        return true;
    }

    void abandon(uint64_t now_us) {
        miss_unfinished(bursts_, done_, keyed_, now_us);
        armed_ = false;
        started_ = false;
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

    void transmit(const ports::RfBurst& burst) {
        keyed_++;
        tx_at_us_ = burst.at_us;
        (void)radio_.transmit(burst.chips, burst.len);
        keyed_at_us_ = clock_.micros();
    }

    // INFO: fc 15sep26 virtual time stands still in a pass, so the run of reads is the window
    int8_t sample_carrier() {
        if (radio_.mode() != parts::RadioMode::Rx) return carrier_.dbm;
        int8_t window[timing::ChannelLevel::kSamples];
        for (uint8_t i = 0; i < timing::ChannelLevel::kSamples; i++) window[i] = radio_.rssi_inst();
        carrier_.dbm = timing::ChannelLevel::mean_dbm(window, timing::ChannelLevel::kSamples);
        carrier_.samples++;
        return carrier_.dbm;
    }

    void drain(uint64_t now_us) {
        for (;;) {
            const parts::RadioEvent ev = radio_.poll(rx_.data.data(), events::kRfEventBytes);
            switch (ev.type) {
                case parts::RadioEventType::None: return;
                case parts::RadioEventType::RxDone: push_rx(ev, now_us); break;
                case parts::RadioEventType::CrcError:
                    emit(events::RfEventType::CrcError, now_us, ev);
                    break;
                case parts::RadioEventType::TxDone:
                    done_++;
                    emit(events::RfEventType::TxDone, now_us);
                    (void)radio_.start_receive();
                    break;
                default: emit(events::RfEventType::Missed, now_us); return;
            }
        }
    }

    // The frame is already in the event that will carry it. An O-band uplink
    // codeword is 255 bytes, and staging one on the stack as well as on the
    // queue would be the same bytes twice. The band the dwell was armed for
    // travels with it: the O band carries one system and the M band two, and
    // only the arming knows which of them this burst is.
    void push_rx(const parts::RadioEvent& ev, uint64_t now_us) {
        rx_.type = events::RfEventType::RxDone;
        rx_.band = band_;
        rx_.freq_hz = freq_hz_;
        rx_.len = ev.len;
        rx_.rssi_dbm = ev.rssi_dbm;
        rx_.rssi_valid = ev.rssi_valid;
        rx_.at_us = now_us;
        out_.push(rx_);
    }

    void emit(events::RfEventType type, uint64_t now_us, const parts::RadioEvent& ev = {}) {
        events::RfEvent e{};
        e.type = type;
        e.band = band_;
        e.freq_hz = freq_hz_;
        e.rssi_dbm = ev.rssi_dbm;
        e.rssi_valid = ev.rssi_valid;
        e.at_us = now_us;
        e.keyed_at_us = keyed_at_us_;
        e.tx_at_us = tx_at_us_;
        out_.push(e);
    }

    parts::Sx1262& radio_;
    ports::Clock& clock_;
    bus::Queue<events::RfEvent, 8>& out_;
    ports::RfPlan plan_{};
    ports::RfPlan pending_{};
    ports::RfBursts bursts_{};
    ports::RfBursts pending_bursts_{};
    ports::RfCarrier carrier_{};
    ports::RfSwitching switching_{};
    ports::RfMode last_mode_{ports::RfMode::Idle};
    uint32_t last_freq_hz_{0};
    uint64_t armed_at_us_{0};
    uint64_t pending_armed_at_us_{0};
    events::RfEvent rx_{};
    model::Band band_{model::Band::M};
    uint32_t freq_hz_{0};
    uint64_t keyed_at_us_{0};
    uint64_t tx_at_us_{0};
    uint32_t last_ms_{0};
    uint32_t armed_count_{0};
    int sleeps_{0};
    bool armed_{false};
    bool has_pending_{false};
    bool started_{false};
    uint8_t keyed_{0};
    uint8_t done_{0};
};

}  // namespace skyblip::platform::host

#endif
