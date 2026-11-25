#include "core/protocol/adsl.h"

#include <optional>

#include "core/flight/state.h"
#include "core/gnss/nmea.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/soft_versioning.h"
#include "protocols/common/geometry.h"

namespace skyblip::protocol {

bool to_observation(const adsl::Packet& packet, const events::Stamp& received, int8_t rssi_dbm,
                    model::Source source, model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    adsl::Traffic traffic;
    if (adsl::decode_traffic(packet, traffic) != wire::Status::Ok || !traffic.position_valid)
        return false;
    out.address = traffic.address;
    out.address_mapping_table = packet.address_mapping_table();
    out.aircraft_category = traffic.aircraft_category;
    out.flight_state = traffic.flight_state;
    out.emergency = traffic.emergency;
    out.latitude_1e7 = traffic.latitude_1e7;
    out.longitude_1e7 = traffic.longitude_1e7;
    out.altitude_valid = traffic.altitude_valid;
    out.altitude_m = traffic.altitude_m;
    out.climb_valid = traffic.climb_valid;
    out.climb_e8 = traffic.climb_e8;
    out.speed_valid = traffic.speed_valid;
    out.speed_q = traffic.speed_q;
    out.track_c9 = traffic.track_c9;
    out.received = received;
    out.rssi_dbm = rssi_dbm;
    out.source = source;
    out.position_valid = true;
    const std::optional<uint8_t> version = adsl::traffic_max_version(packet);
    if (!version) return false;
    out.adsl_version = *version;
    return true;
}

// INFO: fc 18nov26 the VFOM is G.1.14's model taken to VDOP, as integrity_from_dop_e2 takes it
bool issue1_form(adsl::Packet& packet, const model::Ownship& own) {
    const std::optional<uint32_t> vfom_cm =
        own.vdop_e2 == 0 ? std::nullopt : std::optional<uint32_t>(adsl::hfom_cm(own.vdop_e2));
    return adsl::issue1_form(packet, vfom_cm);
}

// INFO: fc 13sep26 no VDOP is a 2D solution, whose height is not a figure to claim accuracy for
adsl::Integrity integrity_from_dop_e2(uint16_t hdop_e2, uint16_t vdop_e2) {
    adsl::Integrity integrity;
    if (hdop_e2 == 0) return integrity;
    const uint32_t hfom_cm = adsl::hfom_cm(hdop_e2);
    // INFO: fc 23sep26 G.1.15 gives no VFOM model, so skyBlip takes G.1.14's to VDOP as well
    const uint32_t vfom_cm = adsl::hfom_cm(vdop_e2);
    integrity.horizontal_accuracy = adsl::Packet::horizontal_accuracy_code(hfom_cm);
    integrity.vertical_accuracy = vdop_e2 == 0 ? 0 : adsl::Packet::vertical_accuracy_code(vfom_cm);
    integrity.vertical_accuracy_issue2_only =
        vdop_e2 != 0 &&
        integrity.vertical_accuracy != adsl::Packet::issue1_vertical_accuracy_code(vfom_cm);
    return integrity;
}

bool from_own_status(adsl::Packet& packet, settings::AirAddress address,
                     uint8_t address_mapping_table, const adsl::StatusReport& report) {
    if (!address) return false;
    adsl::encode_status(packet, *address & settings::kAddressMask, address_mapping_table, report);
    return true;
}

bool from_own(adsl::Packet& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category) {
    if (!address) return false;
    // INFO: fc 18nov26 G.1.1: the timestamp names the solution's time within 10 ms, or none
    const std::optional<int32_t> named_ms = gnss::named_quarter_ms(own.utc_ms);
    if (!named_ms) return false;
    adsl::Traffic traffic;
    traffic.address = *address & settings::kAddressMask;
    traffic.timestamp = adsl::timestamp_code(own.utc_s, *named_ms);
    traffic.flight_state = flight::announced_state(own.flight_state, aircraft_category);
    traffic.aircraft_category = aircraft_category;
    traffic.emergency = adsl::kEmergencyNone;
    traffic.position_valid = own.fix_valid;
    traffic.latitude_1e7 = own.latitude_1e7;
    traffic.longitude_1e7 = own.longitude_1e7;

    // INFO: fc 20nov26 G.1.7, G.1.8: with no fix, altitude and speed go unavailable, never stale
    if (own.fix_valid) {
        // INFO: fc 22nov26 G.1.7: a VDOP comes only from a 3D GSA set under 1.5 s old
        traffic.altitude_valid = own.vdop_e2 != 0;
        if (traffic.altitude_valid)
            traffic.altitude_m = to_metres(Millimetres(own.altitude_hae_mm)).value;
        // INFO: fc 22nov26 G.1.10 has no invalid track, so a speed with none goes invalid unless 0
        traffic.speed_valid = model::velocity_valid(own);
        if (traffic.speed_valid)
            traffic.speed_q = to_speed_q(MillimetresPerSecond(own.speed_mm_s)).value;
        // INFO: fc 19nov26 VDOP cannot bound a geoid separation the parser had to assume
        traffic.integrity =
            integrity_from_dop_e2(own.hdop_e2, own.geoid_measured ? own.vdop_e2 : 0);
    }

    // INFO: fc 20nov26 G.1.9: climb has its own validity, since a 0 claims level flight
    traffic.climb_valid = own.climb_valid;
    if (own.climb_valid) traffic.climb_e8 = to_climb_e8(MillimetresPerSecond(own.climb_mm_s)).value;

    if (own.track_valid) traffic.track_c9 = to_cordic9(CentiDegrees(own.track_cdeg)).value;
    adsl::encode_traffic(packet, address_mapping_table, traffic);
    return true;
}

}
