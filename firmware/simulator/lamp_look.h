#ifndef SKYBLIP_SIMULATOR_LAMP_LOOK_H
#define SKYBLIP_SIMULATOR_LAMP_LOOK_H

#include "core/indication/lamp.h"
#include "hardware/platform/host/indicator.h"

namespace skyblip::simulator {

// INFO: fc 09nov26 a 30 ms wink can fall between two browser frames, so a look keeps it
class LampLook {
   public:
    explicit LampLook(const platform::host::Indicator& indicator) : indicator_(indicator) {}

    void watch() {
        if (indicator_.lit()) lit_since_look_ = indicator_.lamp();
    }

    indication::Lamp since_last_look() {
        const indication::Lamp seen =
            lit_since_look_ != indication::Lamp::None ? lit_since_look_ : indicator_.lamp();
        lit_since_look_ = indication::Lamp::None;
        return seen;
    }

   private:
    const platform::host::Indicator& indicator_;
    indication::Lamp lit_since_look_{indication::Lamp::None};
};

}  // namespace skyblip::simulator

#endif
