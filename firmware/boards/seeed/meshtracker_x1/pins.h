#ifndef SKYBLIP_BOARDS_MESHTRACKER_X1_PINS_H
#define SKYBLIP_BOARDS_MESHTRACKER_X1_PINS_H

#include <cstdint>

#include "core/power/shutdown.h"

#ifdef CONFIG_BOARD_MESHTRACKER_X1
#include <zephyr/devicetree.h>
#endif

namespace skyblip::boards::meshtracker_x1 {

constexpr int pin_number(int port, int pin) { return port * 32 + pin; }

// TODO: fc 12nov26 SoftRF Seeed_X1.h and Meshtastic variant.h, unverified until #169 benches a unit
constexpr int kRadioBusy = pin_number(0, 7);
constexpr int kRadioReset = pin_number(1, 10);
constexpr int kRadioIrq = pin_number(1, 1);
// TODO: fc 12nov26 DIO8 from SoftRF radiolib.cpp:2132, unverified until #169 benches a unit
constexpr uint8_t kRadioIrqDio = 8;
constexpr int kHapticEnable = pin_number(1, 5);

// TODO: fc 12nov26 the T-Echo's debounce, kept until #169 scopes the X1's button bounce
constexpr uint32_t kButtonHoldMs = 30;

// INFO: fc 12nov26 the factory bootloader enters UF2 only after a 3 s hold: factory/README.md
constexpr power::DfuButton kFactoryDfuButton = power::DfuButton::HoldEntersDfu;

#ifdef CONFIG_BOARD_MESHTRACKER_X1
#define SKYBLIP_DEVICETREE_PIN(node_id) \
    (DT_PROP(DT_GPIO_CTLR(node_id, gpios), port) * 32 + DT_GPIO_PIN(node_id, gpios))

static_assert(kRadioBusy == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(radio_busy_gpio)));
static_assert(kRadioReset == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(radio_reset_gpio)));
static_assert(kRadioIrq == SKYBLIP_DEVICETREE_PIN(DT_NODELABEL(radio_irq_gpio)));
static_assert(kHapticEnable == SKYBLIP_DEVICETREE_PIN(DT_ALIAS(haptic_enable)));

#undef SKYBLIP_DEVICETREE_PIN
#endif

}  // namespace skyblip::boards::meshtracker_x1

#endif
