#ifndef SKYBLIP_PORTS_HAPTIC_H
#define SKYBLIP_PORTS_HAPTIC_H

namespace skyblip::ports {

class Haptic {
   public:
    virtual ~Haptic() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
};

}  // namespace skyblip::ports

#endif
