#ifndef SKYBLIP_PROTOCOLS_ADSL_EDGE_VERSION_H
#define SKYBLIP_PROTOCOLS_ADSL_EDGE_VERSION_H

#include "protocols/common/version_triple.h"

namespace skyblip::adsl_edge {

inline constexpr char kVersion[] = "0.1.0";
inline constexpr char kSpecification[] =
    "OGN payload 66 registration record, nrf52-ogn-tracker src/adsl.h, on ADS-L 4 SRD-860 Issue 2 "
    "framing";

static_assert(wire::is_version_triple(kVersion), "lib/radio_protocols.rb reads major.minor.patch");
static_assert(sizeof(kSpecification) > 1);

}

#endif
