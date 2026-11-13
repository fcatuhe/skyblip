// core/util/intmath.h: fixed-point integer trig & distance. Fixed-point keeps
#ifndef SKYBLIP_CORE_UTIL_INTMATH_H
#define SKYBLIP_CORE_UTIL_INTMATH_H

#include <cstdint>

#include "protocols/common/geometry.h"

namespace skyblip {

using wire::cosine;
using wire::divide_rounded;
using wire::hypotenuse;
using wire::kAngle16PerTurn;
using wire::kE7PerTurn;
using wire::kMicrometresPerE7;
using wire::kTrigOne;
using wire::latitude_angle16;
using wire::sine;
using wire::square_root;
using wire::wrapped_longitude_1e7;

int16_t arctangent(int32_t y, int32_t x);

}

#endif
