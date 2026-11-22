#ifndef SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H
#define SKYBLIP_SIMULATOR_BRAVO_SIMULATOR_H

#include <deque>
#include <string>

#include "hardware/platform/host/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "simulator/phone_stream.h"
#include "simulator/unit_simulator.h"

namespace skyblip::simulator {

class BravoSimulator : public UnitSimulator<BravoSimulator, platform::host::MeshtrackerX1,
                                            bravo::Product<platform::host::MeshtrackerX1>> {
   public:
    using Product = bravo::Product<platform::host::MeshtrackerX1>;

    const std::deque<std::string>& sentences() const { return phone_.sentences(); }
    uint32_t sentences_read() const { return phone_.sentences_read(); }
    const PhoneStream& phone() const { return phone_; }

   private:
    friend UnitSimulator;

    void after_pass() { phone_.read(platform_.link()); }
    void after_load() {}

    PhoneStream phone_{};
};

}  // namespace skyblip::simulator

#endif
