#include "core/gnss/sky.h"

namespace skyblip::gnss {

const char* system_name(System system) {
    switch (system) {
        case System::Gps: return "GPS";
        case System::Glonass: return "GLO";
        case System::Beidou: return "BDS";
        case System::Qzss: return "QZS";
        case System::Unknown: break;
    }
    return "SAT";
}

System system_of_talker(const char* talker, uint8_t id) {
    if (talker[0] == 'G' && talker[1] == 'P')
        return id >= kQzssFirstId ? System::Qzss : System::Gps;
    if (talker[0] == 'G' && talker[1] == 'L') return System::Glonass;
    if (talker[0] == 'B' && talker[1] == 'D') return System::Beidou;
    if (talker[0] == 'G' && talker[1] == 'B') return System::Beidou;
    if (talker[0] == 'G' && talker[1] == 'Q') return System::Qzss;
    if (talker[0] == 'G' && talker[1] == 'A') return System::Unknown;
    return System::Unknown;
}

System system_of_gsa_id(uint8_t nmea_system_id) {
    switch (nmea_system_id) {
        case 1: return System::Gps;
        case 2: return System::Glonass;
        case 4: return System::Beidou;
        case 5: return System::Qzss;
        default: return System::Unknown;
    }
}

void SkyView::open(System system) {
    int kept = 0;
    for (int i = 0; i < count_; i++)
        if (satellites_[i].system != system) satellites_[kept++] = satellites_[i];
    count_ = kept;
}

void SkyView::add(SatelliteView satellite) {
    if (count_ >= kMaxSatellitesInView) return;
    satellite.in_use = used(satellite.system, satellite.id);
    satellites_[count_++] = satellite;
}

void SkyView::solving(System system, uint8_t id) {
    if (id == 0 || in_use_count_ >= kMaxSatellitesInUse) return;
    if (used(system, id)) return;
    used_[in_use_count_].system = system;
    used_[in_use_count_].id = id;
    in_use_count_++;
}

void SkyView::clear_solution() { in_use_count_ = 0; }

void SkyView::forget_in_view() { count_ = 0; }

bool SkyView::used(System system, uint8_t id) const {
    for (int i = 0; i < in_use_count_; i++) {
        if (used_[i].id != id) continue;
        if (used_[i].system == system || used_[i].system == System::Unknown ||
            system == System::Unknown)
            return true;
    }
    return false;
}

int SkyView::in_view_of(System system) const {
    int satellites = 0;
    for (int i = 0; i < count_; i++)
        if (satellites_[i].system == system) satellites++;
    return satellites;
}

int SkyView::in_use_of(System system) const {
    int satellites = 0;
    for (int i = 0; i < in_use_count_; i++)
        if (used_[i].system == system) satellites++;
    return satellites;
}

}  // namespace skyblip::gnss
