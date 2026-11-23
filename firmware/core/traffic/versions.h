#ifndef SKYBLIP_CORE_TRAFFIC_VERSIONS_H
#define SKYBLIP_CORE_TRAFFIC_VERSIONS_H

#include <array>
#include <cstdint>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "protocols/adsl/transmit.h"

namespace skyblip::traffic {

// INFO: fc 22nov26 B.5: three of G.2.11's 20 s Status periods, so two Statuses missed keep it
constexpr uint32_t kStatusForgetS = 3 * adsl::kStatusPeriodS;

// INFO: fc 22nov26 B.5: one ADS-L client heard direct, keyed on its address table and address
struct Client {
    uint32_t address{0};
    int32_t latitude_1e7{0};
    int32_t longitude_1e7{0};
    int32_t altitude_m{0};
    uint32_t at_ms{0};
    uint32_t traffic_s{0};
    uint32_t declared_s{0};
    int16_t climb_e8{0};
    uint16_t speed_q{0};
    uint16_t track_c9{0};
    uint8_t address_mapping_table{0};
    uint8_t flight_state{0};
    uint8_t traffic_version{0};
    uint8_t declared_version{0};
    bool altitude_valid{false};
    bool speed_valid{false};
    bool climb_valid{false};
    bool placed{false};
    bool declared{false};
    bool used{false};

    // INFO: fc 22nov26 B.5: the Status's Max. Protocol Version, else its Traffic's version
    uint8_t max_version() const { return declared ? declared_version : traffic_version; }
    void place(const model::AircraftObservation& observation);
    model::AircraftObservation observation() const;
};

class VersionTable {
   public:
    static constexpr int kMaxClients = 48;

    void set_own_reference(const model::Ownship& own) { own_ = own; }

    // INFO: fc 22nov26 a Traffic its sender keyed itself: a relayed one names nobody we can hear
    void hear_traffic(const model::AircraftObservation& observation, uint32_t now_s);
    void hear_status(uint8_t address_mapping_table, uint32_t address, uint8_t max_version,
                     uint32_t now_s);
    // INFO: fc 22nov26 now_ms is the instant both sides are carried to, on ports::Clock
    uint8_t readable_version(const model::Ownship& own, uint32_t now_ms) const;
    void age_out(uint32_t now_s);

    int count() const;
    int find(uint8_t address_mapping_table, uint32_t address) const;
    // INFO: fc 22nov26 moves each time a client is filed, so a reader tells news apart
    uint32_t revision() const { return revision_; }
    const Client* at(int i) const { return (i >= 0 && i < kMaxClients) ? &clients_[i] : nullptr; }

   private:
    struct Weight {
        bool close;
        uint8_t version;
        uint32_t age_s;
    };

    static bool close_to(const model::Ownship& own, const Client& client, uint32_t now_ms);
    static bool matters_less(const Weight& weight, const Weight& other);
    Weight weight_of(const Client& client, uint32_t now_s) const;
    void file(const Client& heard, int index, uint32_t now_s);
    int slot_for(const Client& heard, uint32_t now_s) const;

    std::array<Client, kMaxClients> clients_{};
    model::Ownship own_{};
    uint32_t revision_{0};
};

}  // namespace skyblip::traffic

#endif
