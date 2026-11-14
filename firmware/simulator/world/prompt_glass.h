#ifndef SKYBLIP_SIMULATOR_WORLD_PROMPT_GLASS_H
#define SKYBLIP_SIMULATOR_WORLD_PROMPT_GLASS_H

#include <cstdint>

#include "products/skyblip_go/glass.h"

namespace skyblip::simulator {

constexpr int kPromptWordsBytes = 256;

bool draw_prompt(go::Glass& glass, const char* prompt, const char* argument, const char* settings,
                 uint32_t seconds_left, char* words = nullptr, int words_capacity = 0);

}  // namespace skyblip::simulator

#endif
