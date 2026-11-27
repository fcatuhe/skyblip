#ifndef SKYBLIP_SIMULATOR_WORLD_SCENARIO_H
#define SKYBLIP_SIMULATOR_WORLD_SCENARIO_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/protocol/air.h"

namespace skyblip::simulator {

enum class Mode : uint8_t { Device, Demo, Training };

enum class Product : uint8_t { Go, Bravo, Paired };

enum class EventKind : uint8_t {
    Fix,
    Pps,
    Barometer,
    BatteryMv,
    ExternalPower,
    Button,
    Altitude,
    Speed,
    Track,
    Aircraft,
    Companion,
    ExpectAlarmMinimum,
    ExpectTrafficMinimum,
    BravoLink,
    Utc,
};

struct AdsbOut {
    bool fitted{false};
    bool on_ground{false};
    // INFO: fc 13nov26 its velocity messages carry no GNSS-baro difference
    bool pressure_only{false};
    // INFO: fc 25nov26 the aircraft's own transponder, as loud in the cockpit as Bravo hears it
    bool aboard{false};
    uint8_t category_type_code{4};
    uint8_t category{0};
};

struct ScenarioAircraft {
    protocol::System system{protocol::System::AdslDirect};
    AdsbOut adsb{};
    uint32_t address{0};
    std::string callsign;
    double north_m{0};
    double east_m{0};
    double up_m{0};
    double speed_mps{30};
    double track_deg{270};
    // INFO: fc 20nov26 positive to the right
    double turn_dps{0};
    double climb_mps{0};
    // INFO: fc 20nov26 below zero the aircraft picks its own instant, as a conforming transmitter
    // does
    int phase_ms{-1};
    int slot{-1};
};

struct ScenarioEvent {
    uint32_t at_ms{0};
    EventKind kind{EventKind::Fix};
    long value{0};
    // INFO: fc 14nov26 "unit": "bravo" in a paired scenario: the Bravo's fix or phone, not the Go's
    bool on_bravo{false};
};

constexpr const char* kDefaultCallsign = "S-BLIP";

struct Scenario {
    std::string name;
    std::string callsign{kDefaultCallsign};
    Mode mode{Mode::Device};
    Product product{Product::Go};
    int32_t latitude_1e7{485000000};
    int32_t longitude_1e7{85000000};
    int32_t altitude_m{1000};
    int32_t speed_kt{45};
    int32_t track_deg{90};
    int32_t turn_dps{0};
    uint32_t duration_ms{0};
    std::vector<ScenarioAircraft> aircraft;
    std::vector<ScenarioEvent> events;
};

bool parse_scenario(const char* json, int bytes, Scenario& parsed);
bool load_scenario(const char* path, Scenario& out);

}  // namespace skyblip::simulator

#endif
