#ifndef SKYBLIP_SIMULATOR_WORLD_PROMPT_GLASS_H
#define SKYBLIP_SIMULATOR_WORLD_PROMPT_GLASS_H

#include <cstdint>

#include "products/skyblip_go/glass.h"

namespace skyblip::simulator {

constexpr int kPromptWordsBytes = 256;

// INFO: fc 17nov26 what the page knows of the Go its prompt stands on, so it words the alarm alike
struct PromptUnit {
    bool sounds{true};
    bool paired{false};
};

bool draw_prompt(go::Glass& glass, const char* prompt, const char* argument, const char* settings,
                 uint32_t seconds_left, char* words = nullptr, int words_capacity = 0,
                 PromptUnit unit = {});

}  // namespace skyblip::simulator

#endif
