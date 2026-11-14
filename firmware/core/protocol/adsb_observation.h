#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_OBSERVATION_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_OBSERVATION_H

#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "protocols/adsb/assembly.h"
#include "protocols/adsb/message.h"

namespace skyblip::protocol {

void observation_of(const adsb::Track& track, model::AircraftObservation& out);

bool adsb_fold(adsb::Assembly& assembly, const adsb::Message& message, uint32_t at_ms,
               const events::Stamp& received, const adsb::Reference& reference,
               model::AircraftObservation& out);

}

#endif
