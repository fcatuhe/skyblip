#ifndef SKYBLIP_PROTOCOLS_ADSB_VERSION_H
#define SKYBLIP_PROTOCOLS_ADSB_VERSION_H

#include "protocols/common/version_triple.h"

namespace skyblip::adsb {

inline constexpr char kVersion[] = "0.8.2";
inline constexpr char kSpecification[] =
    "1090 MHz extended squitter DF17, read only, after RTCA DO-260B (draft 4.2, 1090-WP30-18)";

static_assert(wire::is_version_triple(kVersion), "lib/radio_protocols.rb reads major.minor.patch");
static_assert(sizeof(kSpecification) > 1);

}

#endif
