#ifndef SKYBLIP_CORE_UTIL_HEX_H
#define SKYBLIP_CORE_UTIL_HEX_H

#include <cstddef>

#include "core/util/span.h"

namespace skyblip::hex {

size_t write(ConstByteSpan bytes, char* out);
bool read(const char* text, ByteSpan out);

}  // namespace skyblip::hex

#endif
