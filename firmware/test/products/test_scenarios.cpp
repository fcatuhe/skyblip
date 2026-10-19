// The committed scenarios are regression fixtures: the same files the browser and
// the terminal load are replayed here, and a training scenario's expectations are
// the assertions. A bug found in flight becomes a file, not a bug report.
#include <initializer_list>
#include <string>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/traffic/alarm.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

const char* kInlineScenario =
    "{\"name\":\"inline\",\"mode\":\"training\",\"alt_m\":900,\"speed_kt\":50,"
    "\"track_deg\":90,\"duration_ms\":6000,"
    "\"aircraft\":[{\"north_m\":600,\"east_m\":200,\"up_m\":30,\"speed_mps\":40,"
    "\"track_deg\":200,\"climb_mps_e1\":-25}],"
    "\"events\":[{\"at_ms\":3000,\"expect_traffic_min\":1},"
    "{\"at_ms\":5000,\"expect_alarm_min\":1},{\"at_ms\":5500,\"fix\":0}]}";

void replay(simulator::Simulator& simulator) {
    const uint32_t until =
        simulator.scenario().duration_ms == 0 ? 10000 : simulator.scenario().duration_ms;
    simulator.run(until);
}

}  // namespace

TEST_CASE("scenario: the parser reads own-ship, traffic and events") {
    simulator::Scenario scenario;
    const int len = static_cast<int>(__builtin_strlen(kInlineScenario));
    REQUIRE(simulator::parse_scenario(kInlineScenario, len, scenario));
    CHECK(scenario.name == "inline");
    CHECK(scenario.mode == simulator::Mode::Training);
    CHECK(scenario.alt_m == 900);
    CHECK(scenario.track_deg == 90);
    CHECK(scenario.duration_ms == 6000);
    REQUIRE(scenario.aircraft.size() == 1);
    CHECK(scenario.aircraft[0].north_m == doctest::Approx(600));
    CHECK(scenario.aircraft[0].track_deg == doctest::Approx(200));
    // Tenths of a metre per second on the wire of the file, m/s in the world.
    CHECK(scenario.aircraft[0].climb_mps == doctest::Approx(-2.5));
    REQUIRE(scenario.events.size() == 3);
    CHECK(scenario.events[0].kind == simulator::EventKind::ExpectTrafficMin);
    CHECK(scenario.events[1].kind == simulator::EventKind::ExpectAlarmMin);
    CHECK(scenario.events[2].kind == simulator::EventKind::Fix);
    CHECK(scenario.events[2].value == 0);
}

// A simulated device is one somebody set up, so it carries a registration unless a file says not.
TEST_CASE("scenario: a loaded scenario names the device, and a file can say which name") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);

    simulator::Scenario sc;
    const int len = static_cast<int>(__builtin_strlen(kInlineScenario));
    REQUIRE(simulator::parse_scenario(kInlineScenario, len, sc));
    simulator.load(sc);
    CHECK(std::string(simulator.product().settings().callsign) == "S-BLIP");

    simulator::Scenario named;
    const char* json = "{\"name\":\"named\",\"callsign\":\"F-CBAY\"}";
    REQUIRE(simulator::parse_scenario(json, static_cast<int>(__builtin_strlen(json)), named));
    simulator.load(named);
    CHECK(std::string(simulator.product().settings().callsign) == "F-CBAY");
}

// The registration goes on the air, so a scenario about a device nobody named has to say so.
TEST_CASE("scenario: an empty callsign in the file leaves the device unnamed") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);

    simulator::Scenario sc;
    const char* json = "{\"name\":\"quiet\",\"callsign\":\"\"}";
    REQUIRE(simulator::parse_scenario(json, static_cast<int>(__builtin_strlen(json)), sc));
    simulator.load(sc);
    CHECK(std::string(simulator.product().settings().callsign).empty());
}

TEST_CASE("scenario: an unparseable scenario is refused, not half-applied") {
    simulator::Scenario scenario;
    CHECK_FALSE(simulator::parse_scenario(nullptr, 0, scenario));
    CHECK_FALSE(simulator::load_scenario("scenarios/does-not-exist.json", scenario));
}

TEST_CASE("scenario: a training scenario's expectations hold when replayed") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator::Scenario sc;
    const int len = static_cast<int>(__builtin_strlen(kInlineScenario));
    REQUIRE(simulator::parse_scenario(kInlineScenario, len, sc));
    simulator.load(sc);
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    CHECK(simulator.product().state().air.rx_ok > 0);
    // The last event pulls the fix, and the firmware must follow it down.
    CHECK_FALSE(simulator.product().state().own.fix_valid);
}

TEST_CASE("scenario: the committed files replay with their expectations met") {
    for (const char* path :
         {"scenarios/head_on.json", "scenarios/gnss_loss.json", "scenarios/slot_timing.json"}) {
        simulator::Simulator simulator;
        REQUIRE(simulator.setup() == Status::Ok);
        REQUIRE_MESSAGE(simulator.load_file(path), path);
        replay(simulator);
        CHECK_MESSAGE(simulator.world().failures() == 0, path);
        CHECK_MESSAGE(simulator.product().state().traffic.count() >= 1, path);
    }
}

// The pinned-phase fixture is the slot map's regression test: two neighbours
// inside the direct dwells are heard, the one in the uplink window is not.
TEST_CASE("scenario: pinned transmit phases land where the dwell map says") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/slot_timing.json"));
    replay(simulator);
    CHECK(simulator.product().state().traffic.count() == 2);
    CHECK(simulator.world().air().deaf() > 0);
    CHECK(simulator.world().air().heard() > 0);
}

TEST_CASE("scenario: a failed expectation is reported, not swallowed") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    simulator::Scenario sc;
    sc.duration_ms = 3000;
    sc.events.push_back(simulator::ScenarioEvent{2000, simulator::EventKind::ExpectAlarmMin, 3});
    simulator.load(sc);
    replay(simulator);

    CHECK(simulator.world().failures() == 1);
    REQUIRE(simulator.world().first_failure() != nullptr);
}

// One dwell, two systems: an ADS-L neighbour and an ALP-TAS neighbour transmit on
// the same two M-band channels, and both have to arrive as targets. Losing the
// ALP-TAS one is the failure this fixture exists to catch, because it is silent:
// the radar simply shows less sky than there is.
TEST_CASE("scenario: ADS-L and ALP-TAS traffic are both heard in the same dwells") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/mixed_traffic.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    const traffic::TrafficTable& table = simulator.product().state().traffic;
    int adsl = 0, alptas = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used) continue;
        if (t->observation.source == model::Source::AdslDirect) adsl++;
        if (t->observation.source == model::Source::Alptas) alptas++;
    }
    CHECK(adsl == 1);
    CHECK(alptas == 1);
}

// The ALP-TAS target the scenario flies must come back with the position and
// identity it was sent with, not merely as a blip: a decrypt that lands on
// plausible-looking garbage is worse than a dropped frame.
TEST_CASE("scenario: an ALP-TAS target decodes to where it actually is") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/mixed_traffic.json"));
    simulator.run(5000);

    const traffic::TrafficTable& table = simulator.product().state().traffic;
    const model::Ownship& own = simulator.product().state().own;
    int found = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used || t->observation.source != model::Source::Alptas) continue;
        found++;
        // 900 m north and 250 m east of us at the start, closing from the south.
        const int64_t dlat = t->observation.lat_1e7 - own.lat_1e7;
        const int32_t north_m = static_cast<int32_t>((dlat * 11132) / 1000000);
        CHECK(north_m > 400);
        CHECK(north_m < 1100);
        CHECK(t->observation.alt_m < to_metres(Millimetres(own.alt_hae_mm)).v);
        // A FLARM-taxonomy address is reported as one, so an EFB labels it IDType 2.
        CHECK(protocol::address_mapping_table_to_idtype(t->observation.address_mapping_table) == 2);
        CHECK(t->observation.position_valid);
    }
    CHECK(found == 1);
}

// The fixture for the way this device gets switched off in the cockpit: three
// gliders working the same thermal, co-altitude, a few hundred metres away, all
// going the same way at the same speed. Adding both speeds together called that
// 60 m/s of closure and held every one of them at "urgent" for as long as the
// climb lasted. They are traffic - they belong on the screen, they must not be
// an alarm - and the assertion is what the annunciator is allowed to say.
TEST_CASE("scenario: a gaggle in one thermal is traffic, not three collisions") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/gaggle.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    const traffic::TrafficTable& table = simulator.product().state().traffic;
    CHECK(table.count() == 3);
    CHECK(simulator.product().state().alarm_level == traffic::Level::None);
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t == nullptr || !t->used) continue;
        CHECK(t->alarm_level == traffic::Level::None);
    }
}

// v1 ships the straight-line closure model and no circling prediction, which is a
// decision and not an oversight: the limitation is pinned here rather than argued
// in a meeting. The two fixtures below are the two sides of a thermal. What they
// measure is deliberately not what we wish happened.
//
// What one encounter did, read off the public surface: the alarm level the
// product published, the level the annunciator was last driven at, and the range
// core/traffic/alarm.h computed from the target's last report. One number comes
// from the world instead - how far apart the two aircraft really were - because
// a limitation stated in the device's own view of the range is not honest: that
// view is up to a second stale, and in a turn a second is 23 m.
namespace {

struct Encounter {
    static constexpr uint32_t kSampleMs = 100;
    // The formation layer needs a few reports before it can call a pair a pair,
    // so everything before this is the device meeting a stranger.
    static constexpr uint32_t kSettledMs = 5000;
    // What the ear gets falls one re-notification window later, because what
    // the device has already said out loud is only taken back after the contact
    // has been outside the window for that whole time (traffic::notify_for).
    static constexpr uint32_t kQuietFromMs = kSettledMs + traffic::kRenotifyFloorMs;

    bool announced{false};
    uint32_t first_ms{0};
    int32_t first_range_m{0};
    int32_t first_true_separation_m{0};
    uint32_t last_spoken_ms{0};
    int beeps_before_settle{0};
    int beeps_after_settle{0};
    int32_t min_true_separation_m{999999};
    uint32_t min_true_separation_ms{0};
    uint8_t level_at_min_true_separation{0};
    int32_t min_reported_m{999999};
    // How far the range the device acts on sits from the truth, averaged over
    // the encounter: the one number that says whether the geometry is aligned.
    int32_t mean_range_error_m{0};
    int32_t traffic_count{0};
};

const traffic::Target* only_target(const bus::State& state) {
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = state.traffic.at(i);
        if (t != nullptr && t->used) return t;
    }
    return nullptr;
}

Encounter measure(simulator::Simulator& simulator) {
    Encounter e{};
    const uint32_t until = simulator.scenario().duration_ms;
    int64_t error_sum = 0;
    int samples = 0;
    uint8_t spoken = 0;
    for (uint32_t t = 0; t <= until; t += simulator::Simulator::kStepMs) {
        simulator.step(t);

        // Every step, not every sample: a call is a few hundred ms and a sample
        // every 100 ms would still miss which pass began it.
        if (simulator.buzzer_level() != spoken) {
            spoken = simulator.buzzer_level();
            if (spoken != 0) {
                e.last_spoken_ms = t;
                ++(t >= Encounter::kQuietFromMs ? e.beeps_after_settle : e.beeps_before_settle);
            }
        }

        if (t % Encounter::kSampleMs != 0) continue;

        const bus::State& state = simulator.product().state();
        const traffic::Target* target = only_target(state);
        if (target == nullptr) continue;

        const traffic::AlarmAssessment a = traffic::assess(state.own, target->observation, t);
        if (!a.valid) continue;
        const uint8_t level = traffic::to_number(state.alarm_level);
        const int32_t range_m = a.rel_dist_m;
        const int32_t true_separation_m = static_cast<int32_t>(simulator.world().separation_m(0));

        if (level >= 1 && !e.announced) {
            e.announced = true;
            e.first_ms = t;
            e.first_range_m = range_m;
            e.first_true_separation_m = true_separation_m;
        }
        if (range_m < e.min_reported_m) e.min_reported_m = range_m;
        error_sum +=
            range_m > true_separation_m ? range_m - true_separation_m : true_separation_m - range_m;
        samples++;
        if (true_separation_m < e.min_true_separation_m) {
            e.min_true_separation_m = true_separation_m;
            e.min_true_separation_ms = t;
            e.level_at_min_true_separation = level;
        }
        e.traffic_count = state.traffic.count();
    }
    if (samples > 0) e.mean_range_error_m = static_cast<int32_t>(error_sum / samples);
    return e;
}

void report(const Encounter& e) {
    MESSAGE("first advisory: t=" << e.first_ms << " ms, model range=" << e.first_range_m
                                 << " m, true separation=" << e.first_true_separation_m << " m");
    MESSAGE("beeps: " << e.beeps_before_settle << " before " << Encounter::kQuietFromMs << " ms, "
                      << e.beeps_after_settle << " after; last at t=" << e.last_spoken_ms << " ms");
    MESSAGE("closest approach: " << e.min_true_separation_m << " m true at t="
                                 << e.min_true_separation_ms << " ms, published level "
                                 << static_cast<int>(e.level_at_min_true_separation)
                                 << ", closest the model ever saw " << e.min_reported_m << " m");
    MESSAGE("mean range error: " << e.mean_range_error_m << " m");
}

}  // namespace

// Decision 5.3, settled: cores 75 m apart, an 11 m crossing that both arcs see coming.
TEST_CASE("scenario: two gliders sharing a thermal core are warned about the 15 m pass") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/circling_gaggle.json"));
    const Encounter e = measure(simulator);
    report(e);

    CHECK(simulator.world().failures() == 0);
    CHECK(e.traffic_count == 1);

    CHECK(e.min_true_separation_m < 15);
    CHECK(e.min_true_separation_ms > 10000);
    CHECK(e.announced);
    CHECK(e.first_ms < e.min_true_separation_ms);

    // A neighbour's turn rate is not on the wire, so each is still carried straight to now.
    CHECK(e.min_reported_m < 60);
    CHECK(e.mean_range_error_m < 20);
}

// The other side of the fence: a glider arriving straight at 45 m/s is spoken
// about while it is still a long way out, and once.
TEST_CASE("scenario: a glider joining the thermal on a straight line is still caught") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/thermal_joiner.json"));
    const Encounter e = measure(simulator);
    report(e);

    CHECK(simulator.world().failures() == 0);
    CHECK(e.traffic_count == 1);

    // Inside the window from the first report it made, and spoken about there:
    // 2 km at 68 m/s of closure is half a minute of looking.
    CHECK(e.announced);
    CHECK(e.first_range_m <= traffic::kAdvisoryDistM);
    CHECK(e.first_range_m > 1800);
    CHECK(e.first_ms < 5000);
    CHECK(e.min_true_separation_ms - e.first_ms > 30000);

    // One pair of beeps for the whole encounter: it arrives once.
    CHECK(e.beeps_before_settle == 2);
    CHECK(e.beeps_after_settle == 0);

    // Both sides carried to the instant the range is read at: unaligned, this encounter reads 57 m
    // out.
    CHECK(e.mean_range_error_m < 40);
}

// The session boundary as a committed fixture: one file, replayed by the tests
// today and by the browser page the day someone wonders where the log came
// from. The device is stationary, then it flies, then it stops - and what the
// partition holds afterwards is exactly one flight with a beginning and an end.
TEST_CASE("scenario: a takeoff and a landing bracket one flight log session") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/takeoff_and_landing.json"));

    uint32_t parked_records = 0;
    uint32_t parked_writes = 0;
    uint32_t airborne_records = 0;
    bool was_recording = false;
    for (uint32_t t = 0; t <= 130000; t += simulator::Simulator::kStepMs) {
        simulator.step(t);
        if (t == 18000) {
            parked_records = simulator.product().flight_log().records_written();
            parked_writes = simulator.platform().log_flash().writes;
        }
        if (t == 70000) {
            was_recording = simulator.product().flight_log().recording();
            airborne_records = simulator.product().flight_log().records_written();
        }
    }

    // Twenty seconds parked: nothing written, no sector erased.
    CHECK(parked_records == 0);
    CHECK(parked_writes == 0);
    CHECK(was_recording);
    CHECK(airborne_records > 0);

    CHECK(simulator.world().failures() == 0);
    CHECK_FALSE(simulator.product().flight_log().recording());
    CHECK(simulator.product().flight_log().records_written() > airborne_records);
    CHECK(simulator.product().flight_log().sessions_on_flash() == 1);
}

// The ground relay as a committed fixture, so the case is reproducible in the
// browser and not only in a test file: three aircraft too far away for this
// device to hear, reaching it because a skyPost heard them and put all three in
// one O-band frame, alongside one neighbour it hears for itself on the M band.
//
// Before 2026-08-05 this scenario produced exactly one target. The uplink frame
// went to protocol::receive_mband, which frames the M band's two systems and
// nothing else, so it failed there and was counted as rx_bad: the feature was
// absent and its absence was indistinguishable from a noisy site.
TEST_CASE("scenario: a skyPost relay puts aircraft on the radar that we cannot hear") {
    simulator::Simulator simulator;
    REQUIRE(simulator.setup() == Status::Ok);
    REQUIRE(simulator.load_file("scenarios/ground_relay.json"));
    replay(simulator);

    CHECK(simulator.world().failures() == 0);
    const bus::State& state = simulator.product().state();

    int direct = 0, relayed = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = state.traffic.at(i);
        if (t == nullptr || !t->used) continue;
        if (t->observation.source == model::Source::AdslDirect) direct++;
        if (t->observation.source == model::Source::AdslUplink) relayed++;
    }
    CHECK(direct == 1);
    CHECK(relayed == 3);

    // One frame a second, three aircraft in each, and none of it counted as an
    // M-band framing failure.
    CHECK(state.air.uplink_frames > 5);
    CHECK(state.air.uplink_bad == 0);
    CHECK(state.air.uplink_targets >= 3 * state.air.uplink_frames);
    CHECK(state.air.rx_ok > 0);

    // The tape says what it heard and where: the relay is on 869.525 and it is
    // read as a relay, not as an unframed burst.
    bool saw_uplink_line = false;
    for (int i = 0; i < simulator.world().air().record_count(); i++) {
        char line[128];
        simulator.world().air().format(i, line, sizeof(line));
        if (std::string(line).find("869.525 RX  ") != std::string::npos &&
            std::string(line).find("UPLINK 3 aircraft") != std::string::npos)
            saw_uplink_line = true;
    }
    CHECK(saw_uplink_line);
}
