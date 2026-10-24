#ifndef SKYBLIP_SIMULATOR_WORLD_LINK_TEXT_H
#define SKYBLIP_SIMULATOR_WORLD_LINK_TEXT_H

#include <cstdint>
#include <string>

#include "core/util/span.h"

namespace skyblip::simulator::link_text {

int frame_from_text(const char* json, uint8_t* out, int capacity);

std::string text_from_frame(ConstByteSpan frame);

inline std::string text_from_frame(const std::string& frame) {
    return text_from_frame(
        ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size()));
}

}  // namespace skyblip::simulator::link_text

#endif
