#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_RANDOM_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_RANDOM_H

#include <cstdint>
#include <deque>

#include "core/identity/messages.h"
#include "ports/random.h"

namespace skyblip::platform::host {

class Random : public ports::Random {
   public:
    bool draw(ByteSpan out) override {
        draws++;
        if (fails) return false;
        for (size_t i = 0; i < out.size(); i++) {
            if (queued.empty()) {
                out[i] = static_cast<uint8_t>(counter_++ * 37u + 11u);
            } else {
                out[i] = queued.front();
                queued.pop_front();
            }
        }
        return true;
    }

    void queue(const identity::Nonce& nonce) {
        queued.insert(queued.end(), nonce.begin(), nonce.end());
    }

    std::deque<uint8_t> queued{};
    int draws{0};
    bool fails{false};

   private:
    uint32_t counter_{0};
};

}  // namespace skyblip::platform::host

#endif
