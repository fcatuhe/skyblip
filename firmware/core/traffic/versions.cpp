#include "core/traffic/versions.h"

#include <algorithm>

#include "core/traffic/alarm.h"
#include "core/traffic/lease.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/soft_versioning.h"

namespace skyblip::traffic {

void Client::place(const model::AircraftObservation& observation) {
    latitude_1e7 = observation.latitude_1e7;
    longitude_1e7 = observation.longitude_1e7;
    altitude_m = observation.altitude_m;
    altitude_valid = observation.altitude_valid;
    climb_e8 = observation.climb_e8;
    climb_valid = observation.climb_valid;
    speed_q = observation.speed_q;
    speed_valid = observation.speed_valid;
    track_c9 = observation.track_c9;
    flight_state = observation.flight_state;
    at_ms = observation.at_ms;
    traffic_s = observation.received.at_s;
    traffic_version = observation.adsl_version;
    placed = true;
}

model::AircraftObservation Client::observation() const {
    model::AircraftObservation observation{};
    observation.address = address;
    observation.address_mapping_table = address_mapping_table;
    observation.source = model::Source::AdslDirect;
    observation.position_valid = placed;
    observation.latitude_1e7 = latitude_1e7;
    observation.longitude_1e7 = longitude_1e7;
    observation.altitude_m = altitude_m;
    observation.altitude_valid = altitude_valid;
    observation.climb_e8 = climb_e8;
    observation.climb_valid = climb_valid;
    observation.speed_q = speed_q;
    observation.speed_valid = speed_valid;
    observation.track_c9 = track_c9;
    observation.flight_state = flight_state;
    observation.at_ms = at_ms;
    observation.received.at_s = traffic_s;
    observation.adsl_version = traffic_version;
    return observation;
}

void VersionTable::hear_traffic(const model::AircraftObservation& observation, uint32_t now_s) {
    const int index = find(observation.address_mapping_table, observation.address);
    Client heard = index >= 0 ? clients_[index] : Client{};
    heard.address = observation.address;
    heard.address_mapping_table = observation.address_mapping_table;
    heard.used = true;
    heard.place(observation);
    file(heard, index, now_s);
}

void VersionTable::hear_status(uint8_t address_mapping_table, uint32_t address, uint8_t max_version,
                               uint32_t now_s) {
    const int index = find(address_mapping_table, address);
    Client heard = index >= 0 ? clients_[index] : Client{};
    heard.address = address;
    heard.address_mapping_table = address_mapping_table;
    heard.used = true;
    heard.declared = true;
    heard.declared_version = max_version;
    heard.declared_s = now_s;
    file(heard, index, now_s);
}

void VersionTable::file(const Client& heard, int index, uint32_t now_s) {
    if (index < 0) index = slot_for(heard, now_s);
    if (index >= 0) clients_[index] = heard;
}

// INFO: fc 22nov26 B.5: full, it drops the far client heard longest ago, never a close one
int VersionTable::slot_for(const Client& heard, uint32_t now_s) const {
    int victim = -1;
    Weight least{};
    for (int i = 0; i < kMaxClients; i++) {
        if (!clients_[i].used) return i;
        const Weight weight = weight_of(clients_[i], now_s);
        if (victim < 0 || matters_less(weight, least)) {
            victim = i;
            least = weight;
        }
    }
    return matters_less(least, weight_of(heard, now_s)) ? victim : -1;
}

bool VersionTable::matters_less(const Weight& weight, const Weight& other) {
    if (weight.close != other.close) return !weight.close;
    if (weight.close && weight.version != other.version) return weight.version > other.version;
    return weight.age_s > other.age_s;
}

VersionTable::Weight VersionTable::weight_of(const Client& client, uint32_t now_s) const {
    uint32_t heard_age_s = UINT32_MAX;
    if (client.placed) heard_age_s = age_s(client.traffic_s, now_s);
    if (client.declared) heard_age_s = std::min(heard_age_s, age_s(client.declared_s, now_s));
    return Weight{close_to(own_, client, own_.fix_at_ms), client.max_version(), heard_age_s};
}

// INFO: fc 13nov26 a neighbour with no valid height counts as level with us, the cautious reading
bool VersionTable::close_to(const model::Ownship& own, const Client& client, uint32_t now_ms) {
    if (!client.placed) return false;
    const AlarmAssessment seen = assess(own, client.observation(), now_ms);
    if (!seen.valid) return false;
    const int32_t vertical_m = seen.relative_altitude_valid ? seen.relative_altitude_m : 0;
    return adsl::close_client(seen.relative_distance_m, vertical_m, seen.closing_mm_s);
}

uint8_t VersionTable::readable_version(const model::Ownship& own, uint32_t now_ms) const {
    uint8_t version = adsl::Packet::kMaxProtocolVersion;
    for (const Client& client : clients_)
        if (client.used && client.max_version() < version && close_to(own, client, now_ms))
            version = client.max_version();
    return version;
}

void VersionTable::age_out(uint32_t now_s) {
    for (Client& client : clients_) {
        if (!client.used) continue;
        if (client.placed && age_s(client.traffic_s, now_s) > forget_s(client.observation()))
            client.placed = false;
        if (client.declared && age_s(client.declared_s, now_s) > kStatusForgetS)
            client.declared = false;
        if (!client.placed && !client.declared) client = Client{};
    }
}

int VersionTable::count() const {
    return static_cast<int>(
        std::count_if(clients_.begin(), clients_.end(), [](const Client& c) { return c.used; }));
}

int VersionTable::find(uint8_t address_mapping_table, uint32_t address) const {
    for (int i = 0; i < kMaxClients; i++) {
        const Client& client = clients_[i];
        if (client.used && client.address == address &&
            client.address_mapping_table == address_mapping_table)
            return i;
    }
    return -1;
}

}  // namespace skyblip::traffic
