#include "core/protocol/adsl_uplink.h"

#include "core/model/aircraft.h"
#include "core/protocol/adsl.h"
#include "protocols/adsl/packet.h"

namespace skyblip::protocol {

namespace {

adsl::Packet traffic_of(const model::AircraftObservation& target, uint8_t timestamp) {
    adsl::Traffic traffic;
    traffic.address = target.address;
    traffic.timestamp = timestamp;
    traffic.flight_state = target.flight_state;
    traffic.aircraft_category = target.aircraft_category;
    traffic.emergency = target.emergency;
    traffic.position_valid = target.position_valid;
    traffic.latitude_1e7 = target.latitude_1e7;
    traffic.longitude_1e7 = target.longitude_1e7;
    traffic.altitude_valid = target.altitude_valid;
    traffic.altitude_m = target.altitude_m;
    traffic.speed_valid = target.speed_valid;
    traffic.speed_q = target.speed_q;
    traffic.climb_valid = target.climb_valid;
    traffic.climb_e8 = target.climb_e8;
    traffic.track_c9 = target.track_c9;
    adsl::Packet packet{};
    adsl::encode_traffic(packet, target.address_mapping_table, traffic);
    return packet;
}

}  // namespace

Status AdslUplink::encode(const model::AircraftObservation* targets, int count, uint32_t utc_s,
                          int32_t into_ms, uint32_t sender_address, uint8_t sender_table,
                          uint8_t out_packet[kPacketBytes]) const {
    if (count < 0 || count > kMaxTargets) return Status::Full;
    adsl::Packet records[kMaxTargets];
    const uint8_t timestamp = adsl::timestamp_code(utc_s, into_ms);
    for (int i = 0; i < count; i++) records[i] = traffic_of(targets[i], timestamp);
    return status_of(codec_.encode(sender_address, sender_table, records, count, out_packet));
}

Status AdslUplink::decode(const uint8_t* packet, size_t bytes, uint32_t utc_s, int32_t into_ms,
                          model::AircraftObservation* targets, int capacity,
                          DecodeStatistics& statistics) const {
    statistics = DecodeStatistics{};
    adsl::Packet records[kMaxTargets];
    adsl::UplinkStatistics heard;
    const Status status = status_of(codec_.decode(packet, bytes, records, kMaxTargets, heard));
    statistics.corrected = heard.corrected;
    statistics.rejected = heard.rejected;
    if (status != Status::Ok) return status;
    int kept = 0;
    for (int i = 0; i < heard.targets && kept < capacity; i++) {
        if (!adsl::uplink_record_current(records[i], utc_s, into_ms)) {
            statistics.stale++;
            continue;
        }
        model::AircraftObservation target{};
        if (!to_observation(records[i], events::Stamp{}, 0, model::Source::AdslUplink, target)) {
            statistics.rejected++;
            continue;
        }
        targets[kept++] = target;
    }
    statistics.targets = kept;
    return Status::Ok;
}

}
