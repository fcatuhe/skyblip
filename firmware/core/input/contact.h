#ifndef SKYBLIP_CORE_INPUT_CONTACT_H
#define SKYBLIP_CORE_INPUT_CONTACT_H

#include <cstdint>

namespace skyblip::input {

class Contact {
   public:
    enum class Edge : uint8_t { None, Down, Up };

    explicit constexpr Contact(uint32_t settle_ms) : settle_ms_(settle_ms) {}

    Edge update(bool level, uint32_t now_ms) {
        if (level != candidate_) {
            candidate_ = level;
            candidate_since_ms_ = now_ms;
            return Edge::None;
        }
        if (level == stable_) return Edge::None;
        if (now_ms - candidate_since_ms_ < settle_ms_) return Edge::None;
        stable_ = level;
        return level ? Edge::Down : Edge::Up;
    }

    bool down() const { return stable_; }

    uint32_t edge_ms() const { return candidate_since_ms_; }

   private:
    uint32_t settle_ms_;
    uint32_t candidate_since_ms_{0};
    bool candidate_{false};
    bool stable_{false};
};

}  // namespace skyblip::input

#endif
