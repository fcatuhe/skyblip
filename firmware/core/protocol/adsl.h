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

// INFO: fc 13nov26 B.5: a Traffic packet re-coded for an older issue, true when its version dropped
bool lower_to(adsl::Packet& packet, uint8_t version);

bool from_own_status(adsl::Packet& packet, settings::AirAddress address,
                     uint8_t address_mapping_table, const adsl::StatusReport& report);

// G.1.16, NACv. A GNSS-only velocity is as good as the position fix behind it,
// which is the relation the reference encoder uses verbatim
// (oss/SoftRF-moshe-braner .../libraries/OGN/ads-l.h:453).
uint8_t velocity_accuracy_code(uint8_t horizontal_code);

adsl::Integrity integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2);

}

#endif
