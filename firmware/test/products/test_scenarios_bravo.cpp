// Bravo's scenarios: the world's ADS-B squitters through the real receiver to a phone's NMEA.
#include <cstring>
#include <string>
#include <vector>

#include "core/model/aircraft.h"
#include "core/traffic/table.h"
#include "doctest/doctest.h"
#include "simulator/bravo_simulator.h"
#include "simulator/simulator.h"

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

bool exported(const simulator::BravoSimulator& simulator, const char* address) {
    for (const auto& pflaa : newest(simulator, "$PFLAA"))
        if (pflaa[6] == address) return true;
    return false;
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

uint32_t squitters_in_twenty_seconds(const simulator::AdsbOut& adsb, double speed_mps) {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator.world().add_aircraft(2000, 0, 0, speed_mps, 90, -1, -1, protocol::System::Unknown, 0,
                                   0, 0, adsb);
    simulator.run(2000);
    const uint32_t before = simulator.world().adsb_sent();
    for (uint32_t now_ms = 2005; now_ms <= 22000; now_ms += simulator::BravoSimulator::kStepMs)
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
    CHECK(exported(simulator, "300002"));
    CHECK(exported(simulator, "300003"));
    CHECK(exported(simulator, "300004"));
    // The airliner at FL370 is outside the table's 500 m band, and the taxiing one has no altitude.
    CHECK_FALSE(exported(simulator, "300001"));
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
    for (const auto& fields : newest(simulator, "$PFLAA"))
        if (fields[6] == "300004")
            CHECK(std::stoi(fields[4]) == doctest::Approx(200).epsilon(0.05));

    const traffic::Target* with_gnss = target_of(simulator.product().state(), 0x300002);
    REQUIRE(with_gnss != nullptr);
    CHECK(with_gnss->observation.height_difference_valid);
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
    for (const auto& fields : newest(bravo, "$PFLAA"))
        if (fields[6] == "300003") CHECK(fields[5] == "1");

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

TEST_CASE("bravo scenario: a dense sky of thirty aircraft is heard whole and gated to the near") {
    simulator::BravoSimulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/bravo_dense_sky.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    CHECK(simulator.world().adsb_heard() == simulator.world().adsb_sent());
    CHECK(simulator.product().adsb().assembly().count() == 30);
    CHECK(simulator.product().adsb().invalid() == 0);
    CHECK(simulator.product().board().receiver().statistics().dropped == 0);
    CHECK(simulator.product().state().traffic.count() < 30);
}

TEST_CASE("bravo scenario: the world squitters at the rates ADS-B version 2 broadcasts") {
    simulator::AdsbOut adsb;
    adsb.fitted = true;
    // The 1090MHz Riddle Table 1.4 over 20 s: 2 Hz positions and velocities, 0.2 Hz identification.
    CHECK(squitters_in_twenty_seconds(adsb, 50) == 40 + 40 + 4);
    adsb.on_ground = true;
    // Still on the ground: 0.2 Hz positions, no airborne velocity, 0.1 Hz identification.
    CHECK(squitters_in_twenty_seconds(adsb, 0) == 4 + 2);
}
