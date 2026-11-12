// Bravo's scenarios: the world's ADS-B squitters through the real receiver to a phone's NMEA.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "simulator/bravo_simulator.h"
#include "simulator/simulator.h"
#include "simulator/world/adsb_out.h"

using namespace skyblip;

namespace {

std::vector<std::string> fields_of(const std::string& sentence) {
    std::vector<std::string> fields;
    const std::string body = sentence.substr(0, sentence.find('*'));
    size_t at = 0;
    for (size_t comma = body.find(','); comma != std::string::npos;
         at = comma + 1, comma = body.find(',', at))
        fields.push_back(body.substr(at, comma - at));
    fields.push_back(body.substr(at));
    return fields;
}

std::vector<std::vector<std::string>> newest(const simulator::BravoSimulator& simulator,
                                             const char* kind) {
    std::vector<std::vector<std::string>> found;
    for (const std::string& sentence : simulator.sentences())
        if (sentence.rfind(kind, 0) == 0) found.push_back(fields_of(sentence));
    return found;
}

std::vector<std::vector<std::string>> pflaa_of(const simulator::BravoSimulator& simulator,
                                               const char* address) {
    std::vector<std::vector<std::string>> found;
    for (const auto& pflaa : newest(simulator, "$PFLAA"))
        if (pflaa[6] == address) found.push_back(pflaa);
    return found;
}

bool exported(const simulator::BravoSimulator& simulator, const char* address) {
    return !pflaa_of(simulator, address).empty();
}

const traffic::Target* target_of(const bus::State& state, uint32_t address) {
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* target = state.traffic.at(i);
        if (target != nullptr && target->used && target->observation.address == address)
            return target;
    }
    return nullptr;
}

void replay(simulator::BravoSimulator& simulator) {
    simulator.run(simulator.scenario().duration_ms);
}

uint32_t squitters_in_twenty_seconds(const simulator::AdsbOut& adsb, double speed_mps,
                                     uint32_t step_ms = simulator::BravoSimulator::kStepMs) {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().add_aircraft(2000, 0, 0, speed_mps, 90, -1, -1, protocol::System::Unknown, 0,
                                   0, 0, adsb);
    simulator.run(2000);
    const uint32_t before = simulator.world().adsb_sent();
    for (uint32_t now_ms = 2000 + step_ms; now_ms <= 22000; now_ms += step_ms)
        simulator.step(now_ms);
    return simulator.world().adsb_sent() - before;
}

}  // namespace

TEST_CASE("bravo scenario: the parser reads the product, ADS-B Out and a phone connecting") {
    const char* json =
        "{\"name\":\"b\",\"product\":\"bravo\",\"aircraft\":["
        "{\"system\":\"adsb\",\"category\":\"A3\",\"pressure_only\":1},"
        "{\"system\":\"adsl_direct\",\"adsb\":1,\"on_ground\":1,\"category\":\"B1\"},"
        "{\"system\":\"alptas\"}],"
        "\"events\":[{\"at_ms\":0,\"companion\":1}]}";
    simulator::Scenario scenario;
    REQUIRE(simulator::parse_scenario(json, static_cast<int>(std::strlen(json)), scenario));
    CHECK(scenario.product == simulator::Product::Bravo);
    REQUIRE(scenario.aircraft.size() == 3);
    CHECK(scenario.aircraft[0].system == protocol::System::Unknown);
    CHECK(scenario.aircraft[0].adsb.fitted);
    CHECK(scenario.aircraft[0].adsb.pressure_only);
    CHECK(scenario.aircraft[0].adsb.category_type_code == 4);
    CHECK(scenario.aircraft[0].adsb.category == 3);
    CHECK(scenario.aircraft[1].system == protocol::System::AdslDirect);
    CHECK(scenario.aircraft[1].adsb.fitted);
    CHECK(scenario.aircraft[1].adsb.on_ground);
    CHECK(scenario.aircraft[1].adsb.category_type_code == 3);
    CHECK_FALSE(scenario.aircraft[2].adsb.fitted);
    REQUIRE(scenario.events.size() == 1);
    CHECK(scenario.events[0].kind == simulator::EventKind::Companion);
}

// Every file committed before Bravo names no product, and must still fly a Go.
TEST_CASE("bravo scenario: a scenario that names no product flies a Go") {
    simulator::Scenario scenario;
    REQUIRE(simulator::load_scenario("scenarios/head_on.json", scenario));
    CHECK(scenario.product == simulator::Product::Go);
}

TEST_CASE("bravo scenario: Bravo alone sends a phone its ADS-B traffic as Source 1") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_alone.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    const auto pflaa = newest(simulator, "$PFLAA");
    REQUIRE_FALSE(pflaa.empty());
    for (const auto& fields : pflaa) {
        REQUIRE(fields.size() == 15);
        CHECK(fields[13] == "1");
    }
    CHECK(exported(simulator, "300001"));
    CHECK(exported(simulator, "300002"));
    CHECK(exported(simulator, "300003"));
    CHECK(exported(simulator, "300004"));
    // The taxiing one sends no altitude, and $PFLAA needs one.
    CHECK_FALSE(exported(simulator, "300005"));
    CHECK(simulator.product().adsb().assembly().count() == 5);
    CHECK(simulator.product().adsb().invalid() == 0);
}

TEST_CASE("bravo scenario: a target with pressure altitude only is placed against own pressure") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_alone.json"));
    simulator.run(6000);

    const traffic::Target* pressure_only = target_of(simulator.product().state(), 0x300004);
    REQUIRE(pressure_only != nullptr);
    CHECK(pressure_only->observation.source == model::Source::Adsb);
    CHECK(pressure_only->observation.pressure_altitude_valid);
    CHECK_FALSE(pressure_only->observation.height_difference_valid);
    const auto pressure_reports = pflaa_of(simulator, "300004");
    REQUIRE_FALSE(pressure_reports.empty());
    for (const auto& fields : pressure_reports)
        CHECK(std::stoi(fields[4]) == doctest::Approx(200).epsilon(0.05));
}

// A GNSS-baro difference of the wrong sign would put this target twice the geoid, 94 m, off.
TEST_CASE("bravo scenario: a target sending its GNSS-baro difference is placed by GNSS height") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_alone.json"));
    simulator.run(6000);

    const traffic::Target* with_gnss = target_of(simulator.product().state(), 0x300002);
    REQUIRE(with_gnss != nullptr);
    CHECK(with_gnss->observation.height_difference_valid);
    const simulator::VirtualAircraft* truth = simulator.world().aircraft_at(1);
    REQUIRE(truth != nullptr);
    const auto reports = pflaa_of(simulator, "300002");
    REQUIRE_FALSE(reports.empty());
    for (const auto& fields : reports) CHECK(std::abs(std::stoi(fields[4]) - truth->up_m) < 12);
}

// ADS-L F.2.3: the same aircraft on both systems carries its ICAO address on both.
TEST_CASE("bravo scenario: an aircraft on ADS-L and ADS-B is ICAO on each, Go and Bravo alike") {
    simulator::BravoSimulator bravo;
    REQUIRE(bravo.setup() == Status::Ok);
    REQUIRE(bravo.load_file("scenarios/bravo_alone.json"));
    bravo.run(6000);
    const traffic::Target* heard_by_bravo = target_of(bravo.product().state(), 0x300003);
    REQUIRE(heard_by_bravo != nullptr);
    CHECK(heard_by_bravo->observation.source == model::Source::Adsb);
    const auto reports = pflaa_of(bravo, "300003");
    REQUIRE_FALSE(reports.empty());
    for (const auto& fields : reports) CHECK(fields[5] == "1");

    simulator::Simulator go;
    REQUIRE(go.setup() == Status::Ok);
    REQUIRE(go.load_file("scenarios/bravo_alone.json"));
    go.run(6000);
    const bus::State& state = go.product().state();
    CHECK(state.traffic.count() == 1);
    const traffic::Target* heard_by_go = target_of(state, 0x300003);
    REQUIRE(heard_by_go != nullptr);
    CHECK(heard_by_go->observation.source == model::Source::AdslDirect);
    CHECK(heard_by_go->observation.address_mapping_table == 5);
}

TEST_CASE("bravo scenario: Bravo without a fix keeps folding and sends the phone no traffic") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_without_fix.json"));

    simulator.run(7000);
    CHECK_FALSE(newest(simulator, "$PFLAA").empty());

    uint32_t now_ms = 7000;
    for (; now_ms <= 12000; now_ms += simulator::BravoSimulator::kStepMs) simulator.step(now_ms);
    const uint32_t folded_blind = simulator.product().adsb().folded();
    for (; now_ms <= 19000; now_ms += simulator::BravoSimulator::kStepMs) simulator.step(now_ms);
    CHECK_FALSE(simulator.product().state().own.fix_valid);
    CHECK(newest(simulator, "$PFLAA").empty());
    const auto pflau = newest(simulator, "$PFLAU");
    REQUIRE_FALSE(pflau.empty());
    CHECK(pflau.back()[3] == "0");
    CHECK(simulator.product().adsb().folded() > folded_blind);

    for (; now_ms <= simulator.scenario().duration_ms; now_ms += simulator::BravoSimulator::kStepMs)
        simulator.step(now_ms);
    CHECK(simulator.world().failures() == 0);
    CHECK_FALSE(newest(simulator, "$PFLAA").empty());
}

TEST_CASE("bravo scenario: a dense sky of thirty aircraft is heard and held whole") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_dense_sky.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    CHECK(simulator.world().adsb_heard() == simulator.world().adsb_sent());
    CHECK(simulator.product().adsb().assembly().count() == 30);
    CHECK(simulator.product().adsb().invalid() == 0);
    CHECK(simulator.product().board().receiver().statistics().dropped == 0);
    CHECK(simulator.product().state().traffic.count() == 30);
}

// The 1090MHz Riddle Table 1.4: airborne 2 Hz and 0.2 Hz, still on the ground 0.2 Hz and 0.1 Hz.
TEST_CASE("bravo scenario: the world squitters at the rates ADS-B version 2 broadcasts") {
    simulator::AdsbOut adsb;
    adsb.fitted = true;
    CHECK(squitters_in_twenty_seconds(adsb, 50) == 40 + 40 + 4);
    adsb.on_ground = true;
    CHECK(squitters_in_twenty_seconds(adsb, 0) == 4 + 2);
}

TEST_CASE("bravo scenario: a step longer than a squitter period loses no squitter") {
    simulator::AdsbOut adsb;
    adsb.fitted = true;
    CHECK(squitters_in_twenty_seconds(adsb, 50, 2000) == 40 + 40 + 4);
}

TEST_CASE("bravo scenario: a category the emitter tables do not hold refuses the scenario") {
    simulator::Scenario scenario;
    for (const char* json : {"{\"aircraft\":[{\"system\":\"adsb\",\"category\":\"A1\"},"
                             "{\"system\":\"adsb\",\"category\":\"D1\"}]}",
                             "{\"aircraft\":[{\"system\":\"adsb\",\"category\":\"A3\"},"
                             "{\"system\":\"adsb\",\"category\":\"A8\"}]}",
                             "{\"aircraft\":[{\"system\":\"adsb\"},"
                             "{\"system\":\"adsb\",\"category\":\"light\"}]}"})
        CHECK_FALSE(simulator::parse_scenario(json, static_cast<int>(std::strlen(json)), scenario));
    CHECK(scenario.aircraft.empty());
}

TEST_CASE("bravo scenario: loading a scenario again counts its squitters from zero") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_alone.json"));
    simulator.run(3000);
    REQUIRE(simulator.world().adsb_sent() > 0);
    REQUIRE(simulator.world().adsb_heard() > 0);

    REQUIRE(simulator.load_file("scenarios/bravo_alone.json"));
    CHECK(simulator.world().adsb_sent() == 0);
    CHECK(simulator.world().adsb_heard() == 0);
}

// A track of -10 degrees is 350, which ADS-L's 9-bit course codes as 350 * 512 / 360, 497.
TEST_CASE("bravo scenario: a negative track flies and is sent as the same track in [0, 360)") {
    const char* json =
        "{\"name\":\"west\",\"aircraft\":[{\"north_m\":-1500,\"east_m\":200,\"up_m\":20,"
        "\"speed_mps\":30,\"track_deg\":-10}]}";
    simulator::Scenario scenario;
    REQUIRE(simulator::parse_scenario(json, static_cast<int>(std::strlen(json)), scenario));
    simulator::Simulator go;
    REQUIRE(go.setup() == Status::Ok);
    go.load(scenario);
    go.run(5000);

    REQUIRE(go.world().aircraft_at(0) != nullptr);
    CHECK(go.world().aircraft_at(0)->track_deg == doctest::Approx(350));
    const traffic::Target* heard = target_of(go.product().state(), 0x300001);
    REQUIRE(heard != nullptr);
    CHECK(heard->observation.track_c9 == 497);
}

// -295 fpm is 4.6 steps of 64, and 4000 ft is past the 3125 ft the field holds.
TEST_CASE("bravo scenario: ADS-B Out rounds a velocity field to its nearest step and saturates") {
    simulator::AdsbFrame frame = simulator::adsb_ground_velocity(0x300001, 5, 10, 10, -295, 4000);
    simulator::adsb_seal(frame.bytes);
    uint8_t chips[protocol::kAdsbChipBytes];
    simulator::adsb_chips(frame.bytes, chips);
    protocol::AdsbMessage message;
    REQUIRE(protocol::adsb_receive(chips, sizeof(chips), protocol::AdsbReception{}, message) ==
            Status::Ok);
    CHECK(message.velocity.climb_fpm.value == -320);
    CHECK(message.velocity.gnss_minus_pressure_ft.saturated);
}
