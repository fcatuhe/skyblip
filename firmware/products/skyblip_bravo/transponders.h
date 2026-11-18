#ifndef SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_TRANSPONDERS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_BRAVO_TRANSPONDERS_H

#include <array>
#include <cstdint>

#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"

namespace skyblip::bravo {

// INFO: fc 18nov26 a reply's address is read from its parity, so it counts only once heard in clear
class Transponders {
   public:
    static constexpr int kMaxTransponders = adsb::Assembly::kMaxAircraft;
    // TODO: fc 18nov26 30 s misses a dozen squitters 2.4 s apart, unverified until #239 benches it
    static constexpr uint32_t kTransponderForgetMs = 30000;
    // INFO: fc 18nov26 an altitude reply counts as long as a silent transponder is kept
    static constexpr uint32_t kAltitudeMaxAgeMs = kTransponderForgetMs;

    bool hear(const adsb::Message& message, uint32_t now_ms);
    void age_out(uint32_t now_ms);

    int count() const;
    int mode_s_only() const;
    int mode_s_only_with_altitude(uint32_t now_ms) const;
    uint32_t unmatched() const { return unmatched_; }

   private:
    struct Transponder {
        bool used{false};
        uint32_t address{0};
        uint32_t heard_ms{0};
        bool extended_squitter{false};
        uint32_t altitude_ms{0};
        bool altitude_heard{false};
    };

    Transponder* find(uint32_t address);
    Transponder* admit(uint32_t address, uint32_t now_ms);

    std::array<Transponder, kMaxTransponders> transponders_{};
    uint32_t unmatched_{0};
};

}  // namespace skyblip::bravo

#endif
