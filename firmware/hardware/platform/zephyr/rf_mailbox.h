#ifndef SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_MAILBOX_H
#define SKYBLIP_HARDWARE_PLATFORM_ZEPHYR_RF_MAILBOX_H
#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#include "ports/rf.h"

namespace skyblip::platform::zephyr {

// INFO: fc 14nov26 both threads share these, so every member moves under the scheduler lock
class RfMailbox {
   public:
    RfMailbox() {
        k_sem_init(&armed_, 0, 1);
        k_sem_init(&wake_, 0, 1);
    }

    // INFO: fc 27sep26 the join, queue and refusal rules are core/timing/README.md's
    Status arm(const ports::RfPlan& plan, uint64_t now_us) {
        Status verdict = Status::Ok;
        k_sched_lock();
        if (joins_flying_dwell(plan, now_us)) {
            if (flying_bursts_.add(burst_of(plan)))
                wake();
            else
                verdict = Status::WouldBlock;
        } else if (flying_ && plan.end_us <= flying_end_us_) {
            verdict = Status::WouldBlock;
        } else if (queued_ && same_dwell(plan, queued_plan_)) {
            if (plan.tx != nullptr && !queued_bursts_.add(burst_of(plan)))
                verdict = Status::WouldBlock;
        } else {
            queued_plan_ = plan;
            queued_bursts_ = ports::RfBursts::of(plan);
            queued_armed_at_us_ = now_us;
            queued_ = true;
            k_sem_give(&armed_);
        }
        k_sched_unlock();
        return verdict;
    }

    void abort() {
        k_sched_lock();
        queued_ = false;
        flying_ = false;
        k_sched_unlock();
    }

    void drop_queued() {
        k_sched_lock();
        queued_ = false;
        k_sched_unlock();
    }

    // INFO: fc 14nov26 a burst claimed for its CCA, or keyed, is the radio thread's and stays
    Status withdraw(uint64_t tx_at_us) {
        Status verdict = Status::WouldBlock;
        k_sched_lock();
        const int flying = flying_ ? flying_bursts_.index_of(tx_at_us) : -1;
        const int queued = queued_ ? queued_bursts_.index_of(tx_at_us) : -1;
        if (flying >= flying_claimed_) {
            flying_bursts_.remove(static_cast<uint8_t>(flying));
            verdict = Status::Ok;
        } else if (queued >= 0) {
            queued_bursts_.remove(static_cast<uint8_t>(queued));
            verdict = Status::Ok;
        }
        k_sched_unlock();
        if (verdict == Status::Ok) wake();
        return verdict;
    }

    // INFO: fc 27sep26 flying from the moment it is taken, so a burst armed during the retune joins
    bool take(ports::RfPlan& plan, uint64_t& armed_at_us) {
        k_sched_lock();
        const bool taken = queued_;
        if (taken) {
            plan = queued_plan_;
            armed_at_us = queued_armed_at_us_;
            queued_ = false;
            flying_bursts_ = queued_bursts_;
            flying_claimed_ = 0;
            flying_mode_ = plan.mode;
            flying_frequency_hz_ = plan.frequency_hz;
            flying_end_us_ = plan.end_us;
            flying_ = true;
        }
        k_sched_unlock();
        return taken;
    }

    void land() { flying_ = false; }

    ports::RfBursts flying_bursts() const {
        k_sched_lock();
        const ports::RfBursts bursts = flying_bursts_;
        k_sched_unlock();
        return bursts;
    }

    // INFO: fc 14nov26 the radio thread takes burst `index` before its CCA, unless it was withdrawn
    bool claim(const ports::RfBurst& burst, uint8_t index) {
        k_sched_lock();
        const bool claimed = flying_bursts_.index_of(burst.at_us) == index;
        if (claimed) flying_claimed_ = index + 1;
        k_sched_unlock();
        return claimed;
    }

    // INFO: fc 14nov26 a burst found busy leaves the dwell unkeyed, its place free for the next
    void release(uint8_t index) {
        k_sched_lock();
        flying_bursts_.remove(index);
        flying_claimed_ = index;
        k_sched_unlock();
    }

    // INFO: fc 28sep26 reinitialising fits the 23 ms bench stall: only the uplink dwell spares it
    bool reinitialisation_affordable() const {
        k_sched_lock();
        const bool affordable = !queued_ || queued_plan_.mode == ports::RfMode::RxOBand;
        k_sched_unlock();
        return affordable;
    }

    int wait_armed(k_timeout_t timeout) { return k_sem_take(&armed_, timeout); }
    void give_armed() { k_sem_give(&armed_); }
    void wake() { k_sem_give(&wake_); }
    void wait_wake(k_timeout_t timeout) { (void)k_sem_take(&wake_, timeout); }

   private:
    bool joins_flying_dwell(const ports::RfPlan& plan, uint64_t now_us) const {
        if (!flying_ || plan.tx == nullptr) return false;
        if (plan.mode != flying_mode_ || plan.frequency_hz != flying_frequency_hz_) return false;
        return plan.tx_at_us >= now_us && plan.tx_at_us < flying_end_us_;
    }

    static ports::RfBurst burst_of(const ports::RfPlan& plan) {
        return ports::RfBurst{plan.tx, plan.tx_bytes, plan.tx_at_us, plan.tx_forced};
    }

    static bool same_dwell(const ports::RfPlan& one, const ports::RfPlan& other) {
        return one.mode == other.mode && one.frequency_hz == other.frequency_hz &&
               one.end_us == other.end_us;
    }

    struct k_sem armed_{};
    struct k_sem wake_{};
    ports::RfPlan queued_plan_{};
    ports::RfBursts queued_bursts_{};
    uint64_t queued_armed_at_us_{0};
    ports::RfBursts flying_bursts_{};
    int flying_claimed_{0};
    uint64_t flying_end_us_{0};
    uint32_t flying_frequency_hz_{0};
    ports::RfMode flying_mode_{ports::RfMode::Idle};
    volatile bool flying_{false};
    bool queued_{false};
};

}  // namespace skyblip::platform::zephyr
#endif  // __ZEPHYR__
#endif
