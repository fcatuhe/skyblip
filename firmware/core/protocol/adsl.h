#ifndef SKYBLIP_CORE_PROTOCOL_ADSL_H
#define SKYBLIP_CORE_PROTOCOL_ADSL_H

#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "protocols/adsl/packet.h"

namespace skyblip::protocol {

bool to_observation(const adsl::Packet& packet, const events::Stamp& received, int8_t rssi_dbm,
                    model::Source source, model::AircraftObservation& out);

// INFO: fc 18oct26 false, packet untouched, without an address: an unactivated unit builds no frame
// | 18nov26 G.1.1: the solution goes as measured, named by a quarter within 10 ms, or not at all
bool from_own(adsl::Packet& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category);

// INFO: fc 13nov26 B.5: re-coded for Issue 1, or false when a field is unknown to Issue 1
bool issue1_form(adsl::Packet& packet, const model::Ownship& own);

bool from_own_status(adsl::Packet& packet, settings::AirAddress address,
                     uint8_t address_mapping_table, const adsl::StatusReport& report);

// INFO: fc 18nov26 G.1.16: the L76K reports no velocity error, so the velocity accuracy stays 0
adsl::Integrity integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2);

}

#endif
