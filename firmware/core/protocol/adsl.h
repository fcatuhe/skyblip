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

// INFO: fc 13sep26 timestamp names the UTC instant, since_fix_ms the interval carried over
struct BurstInstant {
    uint32_t utc_s;
    int32_t into_utc_ms;
    int32_t since_fix_ms;
};

// INFO: fc 18oct26 false, packet untouched, without an address: an unactivated unit builds no frame
bool from_own(adsl::Packet& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category);
bool from_own(adsl::Packet& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category, const BurstInstant& at);

// INFO: fc 13nov26 B.5: re-coded for Issue 1, or false when a field is unknown to Issue 1
bool issue1_form(adsl::Packet& packet, const model::Ownship& own);

bool from_own_status(adsl::Packet& packet, settings::AirAddress address,
                     uint8_t address_mapping_table, const adsl::StatusReport& report);

// INFO: fc 18nov26 G.1.16: the L76K reports no velocity error, so the velocity accuracy stays 0
adsl::Integrity integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2);

}

#endif
