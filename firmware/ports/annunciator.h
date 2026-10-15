// ports/annunciator.h: capability port: buzzer + vibration motor (T-Echo Plus).
#ifndef SKYBLIP_PORTS_ANNUNCIATOR_H
#define SKYBLIP_PORTS_ANNUNCIATOR_H

#include <cstdint>

namespace skyblip::ports {

enum class Loudness : uint8_t { Soft, Loud };

class Annunciator {
   public:
    virtual ~Annunciator() = default;
    virtual void tone(uint16_t hz, Loudness loudness) = 0;
    virtual void vibrate(uint16_t ms) = 0;
    virtual void silence() = 0;
};

}

#endif
