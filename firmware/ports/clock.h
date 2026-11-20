#ifndef SKYBLIP_PORTS_CLOCK_H
#define SKYBLIP_PORTS_CLOCK_H

#include <cstdint>

namespace skyblip::ports {

// INFO: fc 20nov26 milliseconds() wraps every 49.7 days: hold deadlines by the rules in README.md
class Clock {
   public:
    virtual ~Clock() = default;
    virtual uint32_t milliseconds() const = 0;
    virtual uint64_t microseconds() const = 0;
};

}

#endif
