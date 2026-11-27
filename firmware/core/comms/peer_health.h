#ifndef SKYBLIP_CORE_COMMS_PEER_HEALTH_H
#define SKYBLIP_CORE_COMMS_PEER_HEALTH_H

#include "core/comms/peer.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

void write_bravo_position(cbor::Writer& writer, const BravoPosition& position);

// INFO: fc 27nov26 a source without utc_valid dates nothing, so the frame is refused whole
bool read_utc_source(const cbor::Reader& reader, PeerHealth& health);

// INFO: fc 27nov26 false refuses the whole health, as half a position or one out of range does
bool read_bravo_position(const cbor::Reader& reader, PeerHealth& health);

}  // namespace skyblip::comms

#endif
