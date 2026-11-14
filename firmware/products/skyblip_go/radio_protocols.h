#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_RADIO_PROTOCOLS_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_RADIO_PROTOCOLS_H

#include "core/comms/radio_protocols.h"

namespace skyblip::go {

// INFO: fc 14nov26 must list the stacks PROTOCOLS lists, which test_radio_protocols.cpp checks
inline constexpr comms::RadioProtocol kRadioProtocols[] = {comms::RadioProtocol::Adsl,
                                                           comms::RadioProtocol::Alptas};

}  // namespace skyblip::go

#endif
