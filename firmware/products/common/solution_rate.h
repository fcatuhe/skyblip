#ifndef SKYBLIP_PRODUCTS_COMMON_SOLUTION_RATE_H
#define SKYBLIP_PRODUCTS_COMMON_SOLUTION_RATE_H

#include <cstddef>

#include "core/comms/radio_protocols.h"
#include "ports/gnss.h"

namespace skyblip::common {

// INFO: fc 19nov26 ADS-L G.1.1 names a position's quarter, G.1.16 ages it to 500 ms: 2 Hz
template <class Gnss, size_t N>
constexpr bool solution_rate_serves(const comms::RadioProtocol (&protocols)[N],
                                    ports::SolutionRate rate) {
    bool transmits_adsl = false;
    for (const comms::RadioProtocol protocol : protocols)
        transmits_adsl = transmits_adsl || protocol == comms::RadioProtocol::Adsl;
    return Gnss::solves(rate) && (!transmits_adsl || rate == ports::SolutionRate::Half);
}

}  // namespace skyblip::common

#endif
