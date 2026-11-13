#include "core/protocol/adsl_uplink.h"

#include "core/model/aircraft.h"
#include "protocols/adsl/packet.h"

namespace skyblip::protocol {

Status AdslUplink::encode(const model::AircraftObservation* targets, int count, uint8_t key_index,
                          uint8_t out_frame[kFrameBytes]) const {
    adsl::UplinkTarget records[kMaxTargets];
    const int packed = count < 0 || count > kMaxTargets ? 0 : count;
    for (int i = 0; i < packed; i++) {
        records[i].address = targets[i].address;
        records[i].address_mapping_table = targets[i].address_mapping_table;
        records[i].flight_state = targets[i].flight_state;
        records[i].aircraft_category = targets[i].aircraft_category;
        records[i].speed_q = targets[i].speed_q;
        records[i].latitude_1e7 = targets[i].latitude_1e7;
        records[i].longitude_1e7 = targets[i].longitude_1e7;
        records[i].altitude_m = targets[i].altitude_m;
    }
    return status_of(codec_.encode(records, count, key_index, out_frame));
}

Status AdslUplink::decode(const uint8_t frame[kFrameBytes], model::AircraftObservation* targets,
                          int capacity, DecodeStatistics& statistics) const {
    adsl::UplinkTarget records[kMaxTargets];
    const int room = capacity < kMaxTargets ? capacity : kMaxTargets;
    const Status status = status_of(codec_.decode(frame, records, room, statistics));
    if (status != Status::Ok) return status;
    for (int i = 0; i < statistics.targets; i++) {
        model::AircraftObservation& target = targets[i];
        target = model::AircraftObservation{};
        target.address = records[i].address;
        target.address_mapping_table = records[i].address_mapping_table;
        target.flight_state = records[i].flight_state;
        target.aircraft_category = records[i].aircraft_category;
        target.speed_q = records[i].speed_q;
        target.speed_valid = records[i].speed_q != 0;
        target.latitude_1e7 = records[i].latitude_1e7;
        target.longitude_1e7 = records[i].longitude_1e7;
        target.altitude_m = records[i].altitude_m;
        target.position_valid = true;
        target.source = model::Source::AdslUplink;
        target.emergency = adsl::kEmergencyNone;
    }
    return Status::Ok;
}

}
