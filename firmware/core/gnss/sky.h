#ifndef SKYBLIP_CORE_GNSS_SKY_H
#define SKYBLIP_CORE_GNSS_SKY_H

#include <cstdint>

namespace skyblip::gnss {

enum class System : uint8_t { Unknown, Gps, Glonass, Beidou, Qzss };

constexpr int kSystemCount = 5;
constexpr int kMaxSatellitesInView = 32;
constexpr int kMaxSatellitesInUse = 24;

// INFO: fc 18sep26 L76K protocol table 16: GPS 1-32, GLONASS 65-88, BeiDou 1-63, QZSS 193-197
constexpr uint8_t kQzssFirstId = 193;

const char* system_name(System system);

System system_of_talker(const char* talker, uint8_t id);

System system_of_gsa_id(uint8_t nmea_system_id);

struct SatelliteView {
    uint8_t id{0};
    uint8_t elevation_deg{0};
    uint16_t azimuth_deg{0};
    uint8_t cn0_dbhz{0};
    System system{System::Unknown};
    bool in_use{false};
};

class SkyView {
   public:
    void open(System system);
    void add(SatelliteView sat);
    void solving(System system, uint8_t id);
    void clear_solution();
    void forget_in_view();

    int count() const { return n_; }
    const SatelliteView& at(int i) const { return sats_[i]; }

    int in_use() const { return used_n_; }
    int in_view_of(System system) const;
    int in_use_of(System system) const;
    bool used(System system, uint8_t id) const;

   private:
    struct UsedId {
        System system{System::Unknown};
        uint8_t id{0};
    };

    SatelliteView sats_[kMaxSatellitesInView]{};
    UsedId used_[kMaxSatellitesInUse]{};
    int n_{0};
    int used_n_{0};
};

}  // namespace skyblip::gnss

#endif
