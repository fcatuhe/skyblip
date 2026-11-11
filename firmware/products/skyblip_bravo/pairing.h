#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_PAIRING_H

#include <cstdint>
#include <optional>

#include "core/events/central.h"
#include "products/skyblip_bravo/settings.h"

namespace skyblip::bravo {

enum class Heard : uint8_t { Pinned, Committed, Unpinned, Asking, Stranger, Awaited };

// INFO: fc 11nov26 Bravo stores the pin only once its Go says it holds Bravo's (README.md)
class Pairing {
   public:
    // INFO: fc 11nov26 #192's proposal, a press within 30 s, inside the Go's 35 s kAnswerCeilingMs
    static constexpr uint32_t kRequestWindowMs = 30000;
    // INFO: fc 11nov26 room for a Go that pinned on the health and lost the link to reconnect
    static constexpr uint32_t kOfferWindowMs = 30000;

    explicit Pairing(PeerPin& pin) : pin_(pin) {}

    Heard hear(const events::BluetoothAddress& go, bool go_paired, bool new_connection,
               uint32_t now_ms);
    bool ask(const events::BluetoothAddress& go, uint32_t now_ms);
    bool answer(uint32_t now_ms);
    void withdraw(const events::BluetoothAddress& go);
    void tick(uint32_t now_ms);

    bool requested() const { return request_.has_value(); }
    bool pinned() const { return pin_.pinned; }
    bool pinned_to(const events::BluetoothAddress& go) const;
    bool offered_to(const events::BluetoothAddress& go) const;
    bool holds(const events::BluetoothAddress& go) const { return pinned_to(go) || offered_to(go); }

   private:
    struct Ask {
        events::BluetoothAddress go;
        uint32_t since_ms;
    };

    PeerPin& pin_;
    std::optional<Ask> request_{};
    std::optional<Ask> offer_{};
};

}  // namespace skyblip::bravo

#endif
