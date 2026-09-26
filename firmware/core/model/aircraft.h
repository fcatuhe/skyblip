#ifndef SKYBLIP_CORE_MODEL_AIRCRAFT_H
#define SKYBLIP_CORE_MODEL_AIRCRAFT_H

#include <cstdint>

#include "core/events/stamp.h"

namespace skyblip::model {
enum class Source : uint8_t { AdslDirect = 0, AdslUplink = 1, Alptas = 2, Own = 3 };

// What the station log calls a system where it has a column six characters wide,
// and what nearby abbreviates to one where it has a glyph.
constexpr const char* source_word(Source source) {
    switch (source) {
        case Source::AdslDirect: return "ADS-L";
        case Source::AdslUplink: return "UPLINK";
        case Source::Alptas: return "FLARM";
        case Source::Own: return "OWN";
    }
    return "?";
}

constexpr char source_letter(Source source) {
    switch (source) {
        case Source::AdslDirect: return 'A';
        case Source::AdslUplink: return 'U';
        case Source::Alptas: return 'F';
        case Source::Own: return 'O';
    }
    return '?';
}

struct AircraftObs {
    uint32_t addr;
    uint8_t addr_table;
    uint8_t aircraft_cat;
    uint8_t flight_state;
    uint8_t emergency;
    int32_t lat_1e7;
    int32_t lon_1e7;
    int32_t alt_m;
    int16_t climb_e8;
    uint16_t speed_q;
    uint16_t track_c9;
    events::Stamp received;
    // INFO: fc 13sep26 the instant this position was true, on ports::Clock, for the geometry to
    // align
    uint32_t at_ms;
    int8_t rssi_dbm;
    Source source;
    bool climb_valid;
    bool speed_valid;
    bool position_valid;
    // INFO: fc 23sep26 G.1.7 lets a 2D position mark its altitude invalid, so decoders say which
    bool alt_valid{true};
};

}  // namespace skyblip::model

#endif
