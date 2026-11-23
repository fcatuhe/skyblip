#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_RADIO_H

#include <algorithm>
#include <iterator>
#include <optional>

#include "core/diagnostics/payload.h"
#include "core/flight/state.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/radio/log.h"
#include "core/timing/channel.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/timing/transmit_gate.h"
#include "products/skyblip_go/settings.h"
#include "protocols/adsl/media_access.h"
#include "runtime/service.h"

namespace skyblip::go {

// INFO: fc 15sep26 slot policy only, ports::Rf flies it against absolute deadlines and owns the
// chip
class RadioService : public runtime::Service {
   public:
    // INFO: fc 28sep26 under the 403 ms between the two M dwells closing, so one retired dwell does
    static constexpr uint32_t kTxOutcomeMaxAgeMs = 250;
    // INFO: fc 19nov26 three dwells and readings a second, so nine missed is a radio not answering
    static constexpr uint32_t kRadioAnswerMaxAgeMs = 3000;
    // INFO: fc 19nov26 one refused dwell is a transient, a second inside the age a fault
    static constexpr uint32_t kRadioRefusalSamples = 2;

    RadioService(runtime::Context& context, const Settings& settings)
        : runtime::Service(context), settings_(settings) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;
    // INFO: fc 19nov26 the executor flies the queued dwell, so the next pass is due when it starts
    uint32_t due_in_ms(uint32_t now_ms) const override;

    ports::RfMode armed_mode() const { return current_dwell_.mode; }
    uint32_t arm_count() const { return arm_count_; }
    const timing::Transmitter& transmitter() const { return transmitter_; }

    const timing::NoiseFloor& noise_floor() const { return noise_floor_; }
    uint32_t duty_permille(uint32_t now_ms) const {
        return transmitter_.air_time().permille(now_ms);
    }
    bool over_budget() const { return over_budget_; }
    adsl::StatusReport status_report() const;
    // INFO: fc 14nov26 ADS-L §D.3: the last burst forced on a busy channel, which silences 2000 ms
    std::optional<uint64_t> forced_at_us() const { return forced_us_; }

   private:
    static constexpr uint64_t kSecondUs = 1000000;
    static constexpr uint64_t kKeyToDoneUs = uint64_t{timing::kKeyToDoneMs} * 1000;

    enum class Role : uint8_t { Current, Next };
    using Payload = timing::Transmitter::Payload;
    static constexpr int kPayloads = timing::Transmitter::kPayloads;
    static constexpr Payload kEveryPayload[kPayloads] = {Payload::Position, Payload::Status,
                                                         Payload::Callsign};

    struct Armed {
        ports::RfMode mode{ports::RfMode::Idle};
        uint32_t frequency_hz{0};
        uint64_t from_us{0};
        uint64_t until_us{0};
        int64_t origin_us{0};
        uint32_t utc_s{0};
        uint8_t chip_buffer{0};
        bool carried[kPayloads]{};
        uint64_t at_us[kPayloads]{};

        bool carries(Payload payload) const { return carried[static_cast<int>(payload)]; }
        uint64_t at(Payload payload) const { return at_us[static_cast<int>(payload)]; }
        bool carries_any() const {
            return std::any_of(std::begin(carried), std::end(carried), [](bool on) { return on; });
        }
        void carry(Payload payload, uint64_t tx_at_us) {
            carried[static_cast<int>(payload)] = true;
            at_us[static_cast<int>(payload)] = tx_at_us;
        }
        void drop(Payload payload) { carried[static_cast<int>(payload)] = false; }
        Payload first() const {
            Payload earliest = Payload::Position;
            uint64_t earliest_us = UINT64_MAX;
            for (const Payload payload : kEveryPayload) {
                if (carries(payload) && at(payload) < earliest_us) {
                    earliest = payload;
                    earliest_us = at(payload);
                }
            }
            return earliest;
        }
    };

    struct Upcoming {
        timing::SlotPlan plan;
        int64_t origin_us;
    };

    // INFO: fc 14nov26 §D.3's "initial attempt": the first busy assessment of a turn's burst
    struct Contention {
        bool open{false};
        uint32_t turn_s{0};
        uint64_t first_us{0};
    };

    bool identified();
    void judge_transmit(uint32_t now_ms);
    void withdraw_carried();
    bool radio_answers(uint32_t now_ms);
    bool receiver_holds_rate() const;
    // INFO: fc 20nov26 phases read microseconds() (ports/README.md, The millisecond clock wraps)
    int phase_ms() const;
    int phase_at(uint64_t now_us) const;
    int64_t second_origin_us() const;
    // INFO: fc 20nov26 slot 1 spans the UTC second, so its tail belongs to the second it started in
    int64_t dwell_origin_us() const;
    static uint64_t at_us(int64_t origin_us, int phase_ms);
    Upcoming upcoming_after(const timing::SlotPlan& plan, int64_t origin_us) const;
    static bool holds(const Armed& dwell, const timing::SlotPlan& plan);
    static uint64_t instant_us(const timing::Transmitter::Attempt& attempt, int64_t origin_us);
    static uint64_t armed_between(const Armed& dwell, uint64_t from_us, uint64_t to_us);
    uint32_t slot_utc_s(uint32_t now_ms) const;
    int32_t fix_lag_ms(int64_t origin_us) const;
    int32_t fix_in_hand_ms(int64_t origin_us) const;
    bool full_rate() const;
    void count_given_up(uint32_t now_ms);
    void count_held_for_fix(const timing::SlotPlan& plan, int64_t origin_us, uint32_t now_ms);
    void hold_for_fix(uint32_t utc_s);
    void carry_position(const timing::Transmitter::Attempt& position, uint32_t utc_s);
    static ports::RfMode mode_for(const timing::SlotPlan& plan);
    static void listen_for(timing::Band band, ports::RfPlan& plan);
    timing::Transmitter::Attempt attempt(const timing::SlotPlan& plan, int64_t origin_us,
                                         uint32_t now_ms) const;
    timing::Transmitter::Attempt attempt(const timing::SlotPlan& plan, int64_t origin_us,
                                         uint32_t now_ms, Payload payload) const;
    bool owes(const Armed& dwell, const timing::SlotPlan& plan, int64_t origin_us,
              uint32_t now_ms) const;
    uint8_t encode(const timing::Transmitter::Attempt& attempt, uint64_t tx_at_us, uint32_t utc_s,
                   uint8_t* chips);
    bool transmit_due(const timing::SlotPlan& plan, int64_t origin_us, uint32_t now_ms) const;
    void queue_next(const timing::SlotPlan& plan, int64_t origin_us, uint32_t now_ms);
    void promote();
    void fly(const Armed& dwell);
    void credit(uint64_t tx_at_us, uint32_t now_ms);
    void spend(Armed& dwell, Payload payload, uint32_t now_ms);
    void expire(Armed& dwell, uint64_t now_us);
    void miss(Armed& dwell);
    void arm_dwell(const timing::SlotPlan& slot, int64_t origin_us, uint32_t now_ms, Role role);
    void refuse_dwell(const timing::SlotPlan& slot, const timing::Transmitter::Attempt& first,
                      int phase, Role role, bool carried_tx, uint32_t now_ms);
    void record_dwell(const timing::SlotPlan& slot, const timing::Transmitter::Attempt& attempt,
                      int phase, bool armed, bool carries_tx, uint32_t now_ms);
    diagnostics::Refusal refusal_of(const timing::SlotPlan& slot,
                                    const timing::Transmitter::Attempt& attempt, bool armed,
                                    bool carries_tx) const;
    void log_refusal(radio::Verdict outcome, const timing::SlotPlan& slot, uint32_t now_ms);
    void publish_dwell(uint32_t now_ms);
    void accrue_armed();
    void collect_outcome(uint32_t now_ms);
    void take_carrier_samples(uint32_t now_ms);
    void take_switching(uint32_t now_ms);
    void collect_busy(uint32_t now_ms);
    Armed* carrying(uint64_t tx_at_us, Payload& payload);
    void contend(Armed& dwell, Payload payload, const bus::RfState::Busy& busy, uint32_t now_ms);
    void log_access(radio::Verdict verdict, const Armed& dwell, Payload payload,
                    const bus::RfState::Busy& busy, uint32_t now_ms);
    bool rearm(Armed& dwell, Payload payload, uint64_t tx_at_us, bool forced);
    uint64_t last_key_us(const Armed& dwell, Payload payload) const;
    bool fix_too_old_at(uint64_t tx_at_us) const;
    void await_fix(uint32_t utc_s);
    bool silenced(uint64_t tx_at_us) const;
    static uint64_t forced_end_us(uint64_t forced_at_us);
    void silence_after(uint64_t forced_at_us);
    void record_switch(const ports::RfSwitch& change, uint32_t now_ms);
    static diagnostics::SwitchKind kind_of(const ports::RfSwitch& change);

    timing::Transmitter transmitter_{};
    timing::NoiseFloor noise_floor_{};
    // INFO: fc 20nov26 no loopback guard, as SoftRF has: no received frame reaches this buffer
    adsl::Packet outgoing_{};
    // INFO: fc 13nov26 B.5: the second a position was last held, so each held second counts once
    uint32_t held_for_b5_s_{0};
    bool held_for_b5_{false};
    uint32_t held_for_fix_s_{0};
    bool held_for_fix_{false};
    // INFO: fc 19nov26 a position waiting for its fix, lost unless armed before its second ends
    uint32_t fix_wanted_s_{0};
    bool fix_wanted_{false};
    uint32_t redrawn_s_{0};
    bool redrawn_{false};
    // INFO: fc 27sep26 per dwell and payload: queued bursts are built while the current dwell keys
    uint8_t outgoing_chips_[2][kPayloads][protocol::kTxPayloadChipBytes]{};
    Armed current_dwell_{};
    Armed next_dwell_{};
    Armed retired_dwell_{};
    bool next_armed_{false};
    uint64_t accounted_us_{0};
    uint64_t armed_us_{0};
    uint32_t arm_count_{0};
    uint64_t pass_us_{0};
    uint32_t seen_tx_ok_{0};
    uint32_t seen_carrier_samples_{0};
    std::optional<uint32_t> channel_read_ms_{};
    std::optional<uint32_t> dwell_started_ms_{};
    std::optional<uint32_t> dwells_refused_ms_[kRadioRefusalSamples]{};
    bool transmitted_{false};
    uint32_t seen_refused_{0};
    uint32_t seen_switches_{0};
    uint32_t seen_busy_{0};
    Contention contention_[kPayloads]{};
    std::optional<uint64_t> forced_us_{};
    bool over_budget_{false};
    bool held_logged_{false};
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
