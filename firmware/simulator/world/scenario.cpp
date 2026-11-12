#include "simulator/world/scenario.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include "simulator/world/json.h"

namespace skyblip::simulator {

namespace {

// Split "[{...},{...}]" into its object substrings. The flat reader in
// simulator/world/json then handles each object, so nothing here re-implements
// number or string parsing.
std::vector<std::string> objects_in_array(const std::string& text, const char* key) {
    std::vector<std::string> out;
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = text.find(needle);
    if (at == std::string::npos) return out;
    at = text.find('[', at);
    if (at == std::string::npos) return out;

    int depth = 0;
    size_t start = 0;
    for (size_t i = at; i < text.size(); i++) {
        const char character = text[i];
        if (character == ']' && depth == 0) break;
        if (character == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (character == '}') {
            depth--;
            if (depth == 0) out.push_back(text.substr(start, i - start + 1));
        }
    }
    return out;
}

long integer_or(const json::Reader& reader, const char* key, long fallback) {
    long value = 0;
    return reader.read_integer(key, value) ? value : fallback;
}

bool event_from(const json::Reader& reader, ScenarioEvent& out) {
    struct Named {
        const char* key;
        EventKind kind;
    };
    static const Named kKeys[] = {
        {"fix", EventKind::Fix},
        {"pps", EventKind::Pps},
        {"barometer", EventKind::Barometer},
        {"battery_mv", EventKind::BatteryMv},
        {"external_power", EventKind::ExternalPower},
        {"button", EventKind::Button},
        {"altitude_m", EventKind::Altitude},
        {"speed_kt", EventKind::Speed},
        {"track_deg", EventKind::Track},
        {"aircraft", EventKind::Aircraft},
        {"companion", EventKind::Companion},
        {"expect_alarm_minimum", EventKind::ExpectAlarmMinimum},
        {"expect_traffic_minimum", EventKind::ExpectTrafficMinimum},
    };
    out.at_ms = static_cast<uint32_t>(integer_or(reader, "at_ms", 0));
    for (const Named& named : kKeys) {
        if (!reader.has(named.key)) continue;
        out.kind = named.kind;
        out.value = integer_or(reader, named.key, 1);
        return true;
    }
    return false;
}

protocol::System system_from(const std::string& name) {
    if (name == "alptas") return protocol::System::Alptas;
    // Not a fit: an aircraft this device never hears for itself, reaching it
    // only through a skyPost's O-band relay.
    if (name == "adsl_uplink") return protocol::System::AdslUplink;
    // INFO: fc 13nov26 ADS-B Out alone puts nothing on 868
    if (name == "adsb") return protocol::System::Unknown;
    return protocol::System::AdslDirect;
}

// INFO: fc 13nov26 core/protocol/README.md's shorthand: A is type code 4, B 3 and C 2
bool category_from(const std::string& name, AdsbOut& out) {
    if (name.size() != 2 || name[0] < 'A' || name[0] > 'C' || name[1] < '0' || name[1] > '7')
        return false;
    out.category_type_code = static_cast<uint8_t>(4 - (name[0] - 'A'));
    out.category = static_cast<uint8_t>(name[1] - '0');
    return true;
}

bool adsb_from(const json::Reader& reader, protocol::System system, AdsbOut& adsb) {
    adsb.fitted = system == protocol::System::Unknown || integer_or(reader, "adsb", 0) != 0;
    adsb.on_ground = integer_or(reader, "on_ground", 0) != 0;
    adsb.pressure_only = integer_or(reader, "pressure_only", 0) != 0;
    char category[8] = {0};
    return !reader.read_string("category", category, sizeof(category)) ||
           category_from(category, adsb);
}

Product product_from(const std::string& name) {
    return name == "bravo" ? Product::Bravo : Product::Go;
}

Mode mode_from(const std::string& name) {
    if (name == "demo") return Mode::Demo;
    if (name == "training") return Mode::Training;
    return Mode::Device;
}

}  // namespace

bool parse_scenario(const char* json, int bytes, Scenario& parsed) {
    if (json == nullptr || bytes <= 0) return false;
    Scenario out = parsed;
    const std::string text(json, static_cast<size_t>(bytes));
    const json::Reader top(json, bytes);

    char buffer[64] = {0};
    if (top.read_string("name", buffer, sizeof(buffer))) out.name = buffer;
    if (top.read_string("callsign", buffer, sizeof(buffer))) out.callsign = buffer;
    if (top.read_string("mode", buffer, sizeof(buffer))) out.mode = mode_from(buffer);
    if (top.read_string("product", buffer, sizeof(buffer))) out.product = product_from(buffer);

    out.latitude_1e7 = static_cast<int32_t>(integer_or(top, "latitude_1e7", out.latitude_1e7));
    out.longitude_1e7 = static_cast<int32_t>(integer_or(top, "longitude_1e7", out.longitude_1e7));
    out.altitude_m = static_cast<int32_t>(integer_or(top, "altitude_m", out.altitude_m));
    out.speed_kt = static_cast<int32_t>(integer_or(top, "speed_kt", out.speed_kt));
    out.track_deg = static_cast<int32_t>(integer_or(top, "track_deg", out.track_deg));
    out.turn_dps = static_cast<int32_t>(integer_or(top, "turn_dps", out.turn_dps));
    out.duration_ms = static_cast<uint32_t>(integer_or(top, "duration_ms", out.duration_ms));

    for (const std::string& object : objects_in_array(text, "aircraft")) {
        const json::Reader reader(object.c_str(), static_cast<int>(object.size()));
        ScenarioAircraft aircraft;
        if (reader.read_string("system", buffer, sizeof(buffer)))
            aircraft.system = system_from(buffer);
        if (!adsb_from(reader, aircraft.system, aircraft.adsb)) return false;
        aircraft.north_m = static_cast<double>(integer_or(reader, "north_m", 0));
        aircraft.east_m = static_cast<double>(integer_or(reader, "east_m", 0));
        aircraft.up_m = static_cast<double>(integer_or(reader, "up_m", 0));
        aircraft.speed_mps = static_cast<double>(integer_or(reader, "speed_mps", 30));
        aircraft.track_deg = static_cast<double>(integer_or(reader, "track_deg", 270));
        aircraft.turn_dps = static_cast<double>(integer_or(reader, "turn_dps", 0));
        aircraft.climb_mps = static_cast<double>(integer_or(reader, "climb_mps_e1", 0)) / 10.0;
        aircraft.phase_ms = static_cast<int>(integer_or(reader, "phase_ms", -1));
        aircraft.slot = static_cast<int>(integer_or(reader, "slot", -1));
        out.aircraft.push_back(aircraft);
    }

    for (const std::string& object : objects_in_array(text, "events")) {
        const json::Reader reader(object.c_str(), static_cast<int>(object.size()));
        ScenarioEvent event;
        if (event_from(reader, event)) out.events.push_back(event);
    }
    parsed = std::move(out);
    return true;
}

bool load_scenario(const char* path, Scenario& out) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::string text;
    char chunk[512];
    for (;;) {
        const size_t bytes = std::fread(chunk, 1, sizeof(chunk), file);
        text.append(chunk, bytes);
        if (bytes < sizeof(chunk)) break;
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) return false;
    return parse_scenario(text.c_str(), static_cast<int>(text.size()), out);
}

}  // namespace skyblip::simulator
