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
    for (int i = 0; i < n_; i++)
        if (sats_[i].system != system) sats_[kept++] = sats_[i];
    n_ = kept;
}

void SkyView::add(SatelliteView sat) {
    if (n_ >= kMaxSatellitesInView) return;
    sat.in_use = used(sat.system, sat.id);
    sats_[n_++] = sat;
}

void SkyView::solving(System system, uint8_t id) {
    if (id == 0 || used_n_ >= kMaxSatellitesInUse) return;
    if (used(system, id)) return;
    used_[used_n_].system = system;
    used_[used_n_].id = id;
    used_n_++;
}

void SkyView::clear_solution() { used_n_ = 0; }

void SkyView::forget_in_view() { n_ = 0; }

bool SkyView::used(System system, uint8_t id) const {
    for (int i = 0; i < used_n_; i++) {
        if (used_[i].id != id) continue;
        if (used_[i].system == system || used_[i].system == System::Unknown ||
            system == System::Unknown)
            return true;
    }
    return false;
}

int SkyView::in_view_of(System system) const {
    int n = 0;
    for (int i = 0; i < n_; i++)
        if (sats_[i].system == system) n++;
    return n;
}

int SkyView::in_use_of(System system) const {
    int n = 0;
    for (int i = 0; i < used_n_; i++)
        if (used_[i].system == system) n++;
    return n;
}

}  // namespace skyblip::gnss
