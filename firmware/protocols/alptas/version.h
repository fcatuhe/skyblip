#ifndef SKYBLIP_PROTOCOLS_ALPTAS_VERSION_H
#define SKYBLIP_PROTOCOLS_ALPTAS_VERSION_H

#include "protocols/common/version_triple.h"

namespace skyblip::alptas {

inline constexpr char kVersion[] = "0.1.0";
inline constexpr char kSpecification[] =
    "ALP-TAS 2024 frame, version 3, skyBlip's reading of public sources";

static_assert(wire::is_version_triple(kVersion), "lib/radio_protocols.rb reads major.minor.patch");
static_assert(sizeof(kSpecification) > 1);

}

#endif
