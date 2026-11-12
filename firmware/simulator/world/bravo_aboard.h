#ifndef SKYBLIP_SIMULATOR_WORLD_BRAVO_ABOARD_H
#define SKYBLIP_SIMULATOR_WORLD_BRAVO_ABOARD_H

#include <cstddef>
#include <cstdint>

#include "hardware/platform/host/meshtracker_x1.h"
#include "hardware/platform/host/platform.h"
#include "simulator/world/paired_link.h"
#include "simulator/world/scenario.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

// INFO: fc 14nov26 a Bravo in the Go's aircraft: it reads the Go's own-ship and air, hears the sky
class BravoAboard {
   public:
    BravoAboard(World<platform::host::Platform>& world, platform::host::MeshtrackerX1& bravo)
        : world_(world), bravo_(bravo), link_(bravo.link(), world.platform().central()) {}

    void load(const Scenario& scenario);
    void step(uint32_t now_ms);

    void set_fix(bool on) { bravo_.gnss().fix_valid = on; }
    bool fix() const { return bravo_.gnss().fix_valid; }
    // INFO: fc 14nov26 out of range or switched off: the link drops at once and no connect takes it
    void set_link(bool up);
    bool link_up() const { return link_up_; }
    void connect_companion() { bravo_.link().raise_link(kCompanionSession); }
    void disconnect_companion() { bravo_.link().drop_link(kCompanionSession); }

    PairedLink& link() { return link_; }

   private:
    void follow_own_ship();
    void apply_events(uint32_t now_ms);

    // INFO: fc 14nov26 the Go holds session 1 on Bravo's link, so a phone takes the next one
    static constexpr uint16_t kCompanionSession = 2;

    World<platform::host::Platform>& world_;
    platform::host::MeshtrackerX1& bravo_;
    PairedLink link_;
    Scenario scenario_{};
    size_t next_event_{0};
    uint32_t start_ms_{0};
    bool started_{false};
    bool link_up_{true};
};

}  // namespace skyblip::simulator

#endif
