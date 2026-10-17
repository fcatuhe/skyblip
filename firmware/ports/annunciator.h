// ports/annunciator.h: capability port: buzzer + vibration motor (T-Echo Plus).
#ifndef SKYBLIP_PORTS_ANNUNCIATOR_H
#define SKYBLIP_PORTS_ANNUNCIATOR_H

#include <cstdint>

namespace skyblip::ports {

enum class Loudness : uint8_t { Soft, Loud };

// INFO: fc 17oct26 on until told off, and an adapter cuts one left on past kAnnunciatorOnCeilingMs
class Annunciator {
   public:
    virtual ~Annunciator() = default;
    virtual void tone(uint16_t hz, Loudness loudness) = 0;
    virtual void silence() = 0;
    virtual void vibrate() = 0;
    virtual void still() = 0;
};

}

#endif
