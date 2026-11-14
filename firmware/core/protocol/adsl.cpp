#include "core/protocol/adsl.h"

#include <initializer_list>
#include <optional>

#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/units/units.h"
#include "protocols/adsl/flight_state.h"
#include "protocols/adsl/versioning.h"
#include "protocols/common/geometry.h"

namespace skyblip::protocol {

namespace {

// INFO: fc 13nov26 Issue 2 split broader Issue 1 classes, and a model plane has no Issue 1 code
uint8_t issue1_category(uint8_t category) {
    switch (category) {
        case 14: return 0;
        case 15: return 3;
        case 16:
        case 17: return 7;
        default: return category;
    }
}

}  // namespace

bool to_observation(const adsl::Packet& packet, const events::Stamp& received, int8_t rssi_dbm,
                    model::Source source, model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    adsl::Traffic traffic;
    if (!adsl::decode_traffic(packet, traffic) || !traffic.position_valid) return false;
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

// INFO: fc 13nov26 0xFFFFFE is one step from a real 0xFFFFFF, well inside any accuracy we claim
bool issue1_form(adsl::Packet& packet, const model::Ownship& own) {
    if (!packet.is_position()) return false;
    if (!packet.has_altitude() || !packet.has_speed() || !packet.has_climb()) return false;
    packet.aircraft_category = issue1_category(packet.aircraft_category);
    for (const wire::BitField field : {adsl::Packet::kLatitudeField, adsl::Packet::kLongitudeField})
        if (packet.field(field) == adsl::Packet::kIssue1PositionInvalidCode)
            packet.set_field(field, adsl::Packet::kIssue1PositionInvalidCode - 1);
    if (!packet.has_position()) {
        packet.set_field(adsl::Packet::kLatitudeField, adsl::Packet::kIssue1PositionInvalidCode);
        packet.set_field(adsl::Packet::kLongitudeField, adsl::Packet::kIssue1PositionInvalidCode);
    }
    if (own.vdop_e2 != 0)
        packet.vertical_accuracy =
            adsl::Packet::issue1_vertical_accuracy_code(adsl::hfom_cm(own.vdop_e2));
    packet.set_protocol_version(adsl::Packet::kProtocolVersionIssue1);
    return true;
}

uint8_t velocity_accuracy_code(uint8_t horizontal_code) {
    return horizontal_code >= 4 ? static_cast<uint8_t>(horizontal_code - 4) : 0;
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
    integrity.velocity_accuracy = velocity_accuracy_code(integrity.horizontal_accuracy);
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
    return from_own(packet, own, address, address_mapping_table, aircraft_category,
                    BurstInstant{own.utc_s, 0, 0});
}

bool from_own(adsl::Packet& packet, const model::Ownship& own, settings::AirAddress address,
              uint8_t address_mapping_table, uint8_t aircraft_category, const BurstInstant& at) {
    if (!address) return false;
    adsl::Traffic traffic;
    traffic.address = *address & settings::kAddressMask;

    // The burst leaves later than the fix was solved, so the position goes
    // forward to the instant the timestamp names. Past the model's bound the
    // fix goes out as it stands, dated when it was solved: neither half of the
    // pair is allowed to describe an instant the other does not.
    const int32_t named_ms =
        at.into_utc_ms - at.into_utc_ms % static_cast<int32_t>(adsl::kTimestampQuarterMs);
    const flight::Prediction where =
        flight::extrapolate(own, at.since_fix_ms - (at.into_utc_ms - named_ms));
    traffic.timestamp =
        adsl::timestamp_code(at.utc_s, where.valid ? named_ms : at.into_utc_ms - at.since_fix_ms);
    traffic.flight_state = adsl::announced_state(own.flight_state, aircraft_category);
    traffic.aircraft_category = aircraft_category;
    traffic.emergency = adsl::kEmergencyNone;
    traffic.position_valid = own.fix_valid;
    traffic.latitude_1e7 = where.latitude_1e7;
    traffic.longitude_1e7 = where.longitude_1e7;

    // Altitude and ground speed come from the 3D fix, so without one they are
    // UNAVAILABLE and must say so (G.1.7, G.1.8). Transmitting a stale or zeroed
    // altitude as if it were valid is worse than transmitting nothing: a receiver
    // would compute relative vertical separation against it.
    if (own.fix_valid) {
        traffic.altitude_valid = own.vdop_e2 != 0;
        if (traffic.altitude_valid)
            traffic.altitude_m = to_metres(Millimetres(where.altitude_hae_mm)).value;
        traffic.speed_valid = true;
        traffic.speed_q = to_speed_q(MillimetresPerSecond(own.speed_mm_s)).value;
        traffic.integrity = integrity_from_dop_e2(own.hdop_e2, own.vdop_e2);
    }

    // Vertical rate needs two samples over a window, so it arrives later than the
    // fix and has its own validity. Encoding 0 before then would claim level
    // flight (G.1.9).
    traffic.climb_valid = own.climb_valid;
    if (own.climb_valid) traffic.climb_e8 = to_climb_e8(MillimetresPerSecond(own.climb_mm_s)).value;

    traffic.track_c9 = to_cordic9(CentiDegrees(where.track_cdeg)).value;
    adsl::encode_traffic(packet, address_mapping_table, traffic);
    return true;
}

}
