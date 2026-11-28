#ifndef SKYBLIP_CORE_TRAFFIC_ABOARD_H
#define SKYBLIP_CORE_TRAFFIC_ABOARD_H

#include <array>
#include <cstdint>
#include <optional>

namespace skyblip::traffic {

// INFO: fc 13nov26 a frame moves the level a quarter of the way, so one fade decides nothing
constexpr int32_t kLevelDampingSamples = 4;

struct AboardTuning {
    int8_t loud_floor_dbm;
    int lead_db;
    uint32_t lead_hold_ms;
    uint32_t emitter_max_age_ms;
};

// INFO: fc 27nov26 the loudest emitter by a clear lead, held, which the pilot confirms as aboard
template <class Detail, int Tracked>
class Aboard {
   public:
    struct Emitter {
        uint32_t key{0};
        int32_t level_sixteenths_dbm{0};
        uint32_t heard_ms{0};
        bool used{false};
        Detail detail{};
    };

    explicit constexpr Aboard(const AboardTuning& tuning) : tuning_(tuning) {}

    // INFO: fc 27nov26 a frame with no level only writes to an emitter already tracked
    Detail* hear(uint32_t key, bool level_valid, int8_t level_dbm, uint32_t now_ms) {
        Emitter* emitter = find(key);
        if (level_valid) {
            const int32_t level = level_dbm * kSixteenths;
            if (emitter == nullptr) {
                emitter = admit(key, now_ms);
                emitter->level_sixteenths_dbm = level;
            } else {
                emitter->level_sixteenths_dbm +=
                    (level - emitter->level_sixteenths_dbm) / kLevelDampingSamples;
            }
            emitter->heard_ms = now_ms;
        }
        return emitter == nullptr ? nullptr : &emitter->detail;
    }

    // INFO: fc 13nov26 a quiet emitter is freed, so its age never wraps round to live again
    void tick(uint32_t now_ms) {
        for (Emitter& emitter : emitters_)
            if (emitter.used && !live(emitter, now_ms)) emitter = Emitter{};
        const Emitter* loudest = leader(now_ms);
        if (loudest == nullptr) {
            leading_ = false;
            held_ = false;
            return;
        }
        if (!leading_ || loudest->key != leader_key_) {
            leading_ = true;
            leader_key_ = loudest->key;
            leading_since_ms_ = now_ms;
        }
        held_ = now_ms - leading_since_ms_ >= tuning_.lead_hold_ms;
    }

    const Emitter* held() const {
        if (!held_) return nullptr;
        for (const Emitter& emitter : emitters_)
            if (emitter.used && emitter.key == leader_key_) return &emitter;
        return nullptr;
    }

   private:
    static constexpr int32_t kSixteenths = 16;

    Emitter* find(uint32_t key) {
        for (Emitter& emitter : emitters_)
            if (emitter.used && emitter.key == key) return &emitter;
        return nullptr;
    }

    // INFO: fc 13nov26 a full table gives up an emitter gone quiet first, then the faintest one
    Emitter* admit(uint32_t key, uint32_t now_ms) {
        Emitter* chosen = emitters_.data();
        for (Emitter& emitter : emitters_) {
            if (!emitter.used || !live(emitter, now_ms)) {
                chosen = &emitter;
                break;
            }
            if (emitter.level_sixteenths_dbm < chosen->level_sixteenths_dbm) chosen = &emitter;
        }
        *chosen = Emitter{};
        chosen->used = true;
        chosen->key = key;
        return chosen;
    }

    bool live(const Emitter& emitter, uint32_t now_ms) const {
        return emitter.used && now_ms - emitter.heard_ms <= tuning_.emitter_max_age_ms;
    }

    const Emitter* leader(uint32_t now_ms) const {
        const Emitter* loudest = nullptr;
        const Emitter* runner_up = nullptr;
        for (const Emitter& emitter : emitters_) {
            if (!live(emitter, now_ms)) continue;
            if (loudest == nullptr ||
                emitter.level_sixteenths_dbm > loudest->level_sixteenths_dbm) {
                runner_up = loudest;
                loudest = &emitter;
            } else if (runner_up == nullptr ||
                       emitter.level_sixteenths_dbm > runner_up->level_sixteenths_dbm) {
                runner_up = &emitter;
            }
        }
        if (loudest == nullptr ||
            loudest->level_sixteenths_dbm < tuning_.loud_floor_dbm * kSixteenths)
            return nullptr;
        if (runner_up != nullptr &&
            loudest->level_sixteenths_dbm - runner_up->level_sixteenths_dbm <
                tuning_.lead_db * kSixteenths)
            return nullptr;
        return loudest;
    }

    AboardTuning tuning_;
    std::array<Emitter, Tracked> emitters_{};
    uint32_t leader_key_{0};
    uint32_t leading_since_ms_{0};
    bool leading_{false};
    bool held_{false};
};

}  // namespace skyblip::traffic

#endif
