#ifndef SKYBLIP_PORTS_RANDOM_H
#define SKYBLIP_PORTS_RANDOM_H

#include "core/util/span.h"

namespace skyblip::ports {

class Random {
   public:
    virtual ~Random() = default;

    virtual bool draw(ByteSpan out) = 0;
};

}  // namespace skyblip::ports

#endif
