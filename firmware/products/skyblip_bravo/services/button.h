#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_BUTTON_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_SERVICES_BUTTON_H

#include <cstdint>

#include "core/comms/config.h"
#include "products/common/confirm_gesture.h"
#include "products/common/services/alarm.h"
#include "products/skyblip_bravo/pairing_gesture.h"
#include "products/skyblip_bravo/services/peer.h"
#include "runtime/service.h"

namespace skyblip::bravo {

// INFO: fc 12nov26 what a press means on Bravo: answer a prompt, dismiss an alarm, end an upload
class ButtonService : public runtime::Service {
   public:
    ButtonService(runtime::Context& context, comms::ConfigService& config,
                  common::AlarmService& alarm, PeerService& peer)
        : runtime::Service(context), config_(config), alarm_(alarm), peer_(peer) {}

    void tick(uint32_t now_ms) override;

    comms::Pending prompt() const { return prompt_; }
    bool answering() const { return prompt_ != comms::Pending::None; }
    bool armed() const { return confirm_.armed(); }
    bool pairing_armed() const { return pairing_.armed(); }

   private:
    void sync_prompt(uint32_t now_ms);
    void arm_once_quiet(uint32_t now_ms);
    void sync_receiving(uint32_t now_ms);
    bool prompt_readable() const;
    bool pairing_readable() const;
    void press(uint32_t now_ms);
    void resolve(common::Answer answer);
    bool alarm_stands() const { return context_.state.alarm_undismissed != traffic::Level::None; }

    comms::ConfigService& config_;
    common::AlarmService& alarm_;
    PeerService& peer_;
    common::ConfirmGesture confirm_{};
    PairingGesture pairing_{};
    comms::Pending prompt_{comms::Pending::None};
    uint32_t prompt_since_ms_{0};
    uint32_t pressed_ms_{0};
    uint32_t released_ms_{0};
    uint32_t receiving_since_ms_{0};
    bool pressing_{false};
    bool pressed_while_armed_{false};
    bool released_once_{false};
    bool receiving_{false};
};

}  // namespace skyblip::bravo

#endif
