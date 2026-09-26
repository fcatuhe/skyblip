// Harness, not a test: the SX1262 driver wired to its model, pin for pin.
#ifndef SKYBLIP_TEST_SUPPORT_SX1262_RIG_H
#define SKYBLIP_TEST_SUPPORT_SX1262_RIG_H

#include "hardware/parts/sx1262/model.h"
#include "hardware/parts/sx1262/sx1262.h"

namespace skyblip {

inline parts::Sx1262 make(models::Sx1262& f) {
    return parts::Sx1262(f, f, f, f.busy_pin, f.reset_pin, f.dio1_pin);
}

}  // namespace skyblip

#endif
