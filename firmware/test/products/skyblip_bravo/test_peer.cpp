// Bravo serving its Go on the peer endpoint: health, observations and the reference (#192).
#include <cstdint>
#include <string>
#include <vector>

#include "core/comms/link_registry.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "test/support/adsb_frames.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraft = 0x3C6586;
constexpr uint32_t kOtherAircraft = 0x4CA2D1;
constexpr uint32_t kStepMs = 10;
constexpr events::BluetoothAddress kStranger{1, {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}};

struct GoPosition {
    int32_t latitude_1e7{BravoRig::kLatitude1e7};
    int32_t longitude_1e7{BravoRig::kLongitude1e7};
    uint32_t position_age_ms{200};
    uint32_t altitude_hae_age_ms{200};
    bool pressure_altitude_valid{true};
    int32_t pressure_altitude_mm{BravoRig::kOwnAltitudeMm};
    uint32_t pressure_altitude_age_ms{100};
    uint32_t written_s_ago{0};
};

struct Served {
    BravoRig rig{};
    PairedLink go{rig.platform.link()};
    PairedLink* stranger{nullptr};
    std::vector<std::string> frames{};

    explicit Served(bool pinned = true) {
        REQUIRE(rig.setup() == Status::Ok);
        if (pinned) rig.product.settings().peer = bravo::pin_of(PairedLink::kCentralAddress);
        REQUIRE(go.connect());
    }

    void run_for(uint32_t span_ms) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += kStepMs) {
            rig.step();
            go.pump();
            if (stranger != nullptr) stranger->pump();
            rig.now_ms += kStepMs;
            for (std::string frame; go.take(frame);) frames.push_back(link_text(frame));
        }
    }

    uint32_t utc_s() const { return BravoRig::kBaseUtcS + rig.utc_offset_s; }

    void push_time_only() {
        gnss::GnssSolution solution{};
        solution.utc_valid = true;
        solution.utc_s = utc_s();
        solution.sentences = ++rig.fix_updates;
        rig.product.bus().gnss.push(solution);
    }

    std::string claim_json(bool paired, const GoPosition* position, uint32_t own_icao = 0) const {
        std::string json = "{\"command\":\"peer_claim\",\"boot_epoch\":7";
        if (position != nullptr) {
            json += ",\"at_s\":" + std::to_string(utc_s() - position->written_s_ago) +
                    ",\"latitude_1e7\":" + std::to_string(position->latitude_1e7) +
                    ",\"longitude_1e7\":" + std::to_string(position->longitude_1e7) +
                    ",\"position_age_ms\":" + std::to_string(position->position_age_ms) +
                    ",\"altitude_hae_mm\":" + std::to_string(BravoRig::kOwnAltitudeMm) +
                    ",\"altitude_hae_age_ms\":" + std::to_string(position->altitude_hae_age_ms);
            if (position->pressure_altitude_valid)
                json +=
                    ",\"pressure_altitude_mm\":" + std::to_string(position->pressure_altitude_mm) +
                    ",\"pressure_altitude_age_ms\":" +
                    std::to_string(position->pressure_altitude_age_ms);
        }
        if (own_icao != 0) json += ",\"own_icao\":" + std::to_string(own_icao);
        json += ",\"alarm_requested\":false,";
        json += std::string("\"paired\":") + (paired ? "true" : "false") + "}";
        return complete_registry_frame(json);
    }

    void claim(const GoPosition* position, uint32_t own_icao = 0) {
        REQUIRE(go.write(claim_json(true, position, own_icao)) == Status::Ok);
    }

    // One second of the Go's world: Bravo's solution, the Go's claim, then the passes after.
    void second(bool bravo_fix, const GoPosition* go_position) {
        if (bravo_fix)
            rig.push_fix();
        else
            push_time_only();
        claim(go_position);
        run_for(1000);
        rig.utc_offset_s++;
    }

    void seconds(uint32_t count, bool bravo_fix, const GoPosition* go_position) {
        for (uint32_t i = 0; i < count; i++) second(bravo_fix, go_position);
    }

    std::vector<std::string> sent(const char* command) const {
        std::vector<std::string> out;
        for (const std::string& frame : frames)
            if (field(frame, "command") == command) out.push_back(frame);
        return out;
    }

    std::vector<std::string> observations_of(uint32_t address) const {
        std::vector<std::string> out;
        for (const std::string& frame : sent("observation"))
            if (field(frame, "address") == std::to_string(address)) out.push_back(frame);
        return out;
    }

    std::string last_health() const {
        const std::vector<std::string> health = sent("health");
        return health.empty() ? std::string() : health.back();
    }
};

}  // namespace

TEST_CASE("bravo peer: health goes to the pinned Go once a second, whatever the traffic") {
    Served served;
    served.seconds(5, true, nullptr);
    const std::vector<std::string> health = served.sent("health");
    CHECK(health.size() >= 5);
    CHECK(health.size() <= 6);
    CHECK(served.sent("observation").empty());
    const std::string& last = health.back();
    CHECK(field(last, "protocol_version") == "1");
    CHECK(field(last, "boot_epoch") == std::to_string(served.rig.product.peer().boot_epoch()));
    CHECK(field(last, "listening") == "true");
    CHECK(field(last, "utc_valid") == "true");
    CHECK(field(last, "own_icao_set") == "false");
    CHECK(field(last, "alarm_granted") == "false");
    CHECK(field(last, "export_reference") == "own");
    CHECK(field(last, "paired") == "true");
    CHECK(field(last, "last_sequence").empty());
}

TEST_CASE("bravo peer: a Go Bravo has not pinned hears so, and is never the reference") {
    Served served(false);
    const GoPosition far{BravoRig::kLatitude1e7 + 5000000};
    served.seconds(3, true, &far);
    REQUIRE_FALSE(served.sent("health").empty());
    for (const std::string& health : served.sent("health"))
        CHECK(field(health, "paired") == "false");
    CHECK(served.rig.product.gate().choose(served.rig.state().own, served.rig.now_ms).reference ==
          comms::ExportReference::Own);
}

TEST_CASE("bravo peer: without a fix but with UTC, Bravo takes its Go's fresh position") {
    Served served;
    const GoPosition here{};
    served.seconds(2, false, &here);
    CHECK(field(served.last_health(), "export_reference") == "go");
    CHECK(field(served.last_health(), "utc_valid") == "true");

    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, false, &here);
    const std::vector<std::string> observed = served.observations_of(kAircraft);
    REQUIRE(observed.size() == 1);
    CHECK(field(observed[0], "boot_epoch") ==
          std::to_string(served.rig.product.peer().boot_epoch()));
    CHECK(field(observed[0], "sequence") == "0");
    CHECK(field(observed[0], "latitude_1e7") != "");
    CHECK(field(observed[0], "pressure_altitude_ft") == std::to_string(BravoRig::kAltitudeFt));
    CHECK(std::stoul(field(observed[0], "at_s")) >= BravoRig::kBaseUtcS);
    CHECK(field(served.last_health(), "last_sequence") == "0");
    CHECK(served.rig.count_of("$PFLAA") == 0);
}

TEST_CASE("bravo peer: without UTC Bravo says so and exports nothing, even with a fresh Go") {
    Served served;
    const GoPosition here{};
    for (int i = 0; i < 3; i++) {
        served.rig.blind_fix();
        served.claim(&here);
        served.run_for(1000);
    }
    served.rig.hear_aircraft(kAircraft, 2000);
    served.run_for(1000);
    CHECK(field(served.last_health(), "utc_valid") == "false");
    CHECK(served.sent("observation").empty());
}

TEST_CASE("bravo peer: a stale peer_claim is no reference, and Bravo with no fix exports nothing") {
    Served served;
    const GoPosition here{};
    served.seconds(2, false, &here);
    REQUIRE(field(served.last_health(), "export_reference") == "go");

    GoPosition late = here;
    late.written_s_ago = 5;
    served.seconds(2, false, &late);
    CHECK(field(served.last_health(), "export_reference") == "none");
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, false, &late);
    CHECK(served.observations_of(kAircraft).empty());

    served.seconds(2, false, &here);
    CHECK(field(served.last_health(), "export_reference") == "go");
    for (int i = 0; i < 6; i++) {
        served.push_time_only();
        served.run_for(1000);
        served.rig.utc_offset_s++;
    }
    CHECK(field(served.last_health(), "export_reference") == "none");
}

// No gate: a target far above the Go is exported, whatever height the Go's claim carries.
TEST_CASE("bravo peer: a target 2.5 km above the Go is exported, with or without the Go's height") {
    // 10000 ft is about 2.5 km above the Go's 500 m.
    constexpr int32_t kHighFt = 10000;
    Served served;
    GoPosition here{};
    served.seconds(2, false, &here);
    served.rig.hear_aircraft(kAircraft, 2000, kHighFt);
    served.seconds(1, false, &here);
    CHECK_FALSE(served.observations_of(kAircraft).empty());

    here.pressure_altitude_valid = false;
    served.seconds(1, false, &here);
    served.rig.hear_aircraft(kOtherAircraft, 2000, kHighFt);
    served.seconds(1, false, &here);
    CHECK_FALSE(served.observations_of(kOtherAircraft).empty());
}

// GPT's review: one height older than the claim's ceiling threw the whole position away.
TEST_CASE("bravo peer: a stale height keeps the Go's fresh position as the reference") {
    constexpr int32_t kHighFt = 10000;
    Served served;
    GoPosition here{};
    here.position_age_ms = 100;
    here.altitude_hae_age_ms = 5000;
    here.pressure_altitude_valid = false;
    served.seconds(2, false, &here);
    CHECK(field(served.last_health(), "export_reference") == "go");
    const bravo::GateChoice choice =
        served.rig.product.gate().choose(served.rig.state().own, served.rig.now_ms);
    CHECK(choice.own.fix_valid);
    CHECK_FALSE(choice.own.altitude_hae_valid);

    GoPosition stale_pressure = here;
    stale_pressure.pressure_altitude_valid = true;
    stale_pressure.pressure_altitude_age_ms = bravo::GateReference::kClaimMaxAgeMs + 1;
    served.seconds(1, false, &stale_pressure);
    const bravo::GateChoice without_pressure =
        served.rig.product.gate().choose(served.rig.state().own, served.rig.now_ms);
    CHECK(without_pressure.own.fix_valid);
    CHECK_FALSE(without_pressure.own.pressure_altitude_valid);

    served.rig.hear_aircraft(kAircraft, 2000, kHighFt);
    served.seconds(1, false, &stale_pressure);
    CHECK_FALSE(served.observations_of(kAircraft).empty());
}

// GPT's review: a Go whose buffers were full lost the frame another Go took.
TEST_CASE("bravo peer: a Go that blocks keeps what it is owed, the other is not sent it twice") {
    Served served;
    PairedLink second{served.rig.platform.link(), 2, kStranger};
    served.stranger = &second;
    REQUIRE(second.connect());
    REQUIRE(second.write(served.claim_json(true, nullptr)) == Status::Ok);
    served.seconds(2, true, nullptr);
    std::string frame;
    while (second.take(frame)) {
    }

    served.rig.platform.link().hold_session(1, true);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, true, nullptr);
    CHECK(served.observations_of(kAircraft).empty());
    int theirs = 0;
    while (second.take(frame)) theirs += field(link_text(frame), "address") == "3958150";
    CHECK(theirs == 1);

    served.rig.platform.link().hold_session(1, false);
    served.seconds(1, true, nullptr);
    CHECK(served.observations_of(kAircraft).size() == 1);
    while (second.take(frame)) theirs += field(link_text(frame), "address") == "3958150";
    CHECK(theirs == 1);
}

// GPT's and Opus's review: a refused frame enrolled its sender for observations.
TEST_CASE("bravo peer: a central whose peer frame is refused is never sent observations") {
    Served served;
    PairedLink stranger{served.rig.platform.link(), 2, kStranger};
    served.stranger = &stranger;
    REQUIRE(stranger.connect());
    REQUIRE(stranger.write(complete_registry_frame("{\"command\":\"get\"}")) == Status::Ok);
    uint8_t claim[32];
    cbor::Writer writer(claim, sizeof(claim));
    writer.add_code(link::kCommandKey, link::Command::PeerClaim);
    writer.add_unsigned(link::peer_claim_key::kBootEpoch, 7);
    writer.add_boolean(link::peer_claim_key::kAlarmRequested, false);
    const int bytes = writer.finish();
    REQUIRE(bytes > 0);
    REQUIRE(stranger.write(std::string(reinterpret_cast<const char*>(claim),
                                       static_cast<size_t>(bytes))) == Status::Ok);
    served.seconds(2, true, nullptr);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, true, nullptr);
    CHECK(served.rig.product.peer().refused() == 2);
    std::string frame;
    CHECK_FALSE(stranger.take(frame));
    CHECK(served.observations_of(kAircraft).size() == 1);
}

TEST_CASE("bravo peer: a link that comes back does not resend old targets with fresher times") {
    Served served;
    served.seconds(2, true, nullptr);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, true, nullptr);
    REQUIRE(served.observations_of(kAircraft).size() == 1);
    const std::string first_at_s = field(served.observations_of(kAircraft)[0], "at_s");

    served.go.drop();
    for (int i = 0; i < 5; i++) {
        served.rig.push_fix();
        served.run_for(1000);
        served.rig.utc_offset_s++;
    }
    REQUIRE(served.go.connect());
    served.seconds(3, true, nullptr);
    for (const std::string& frame : served.observations_of(kAircraft))
        CHECK(field(frame, "at_s") == first_at_s);

    served.rig.hear_aircraft(kAircraft, 2100);
    served.seconds(1, true, nullptr);
    const std::vector<std::string> observed = served.observations_of(kAircraft);
    CHECK(std::stoul(field(observed.back(), "at_s")) > std::stoul(first_at_s));
}

TEST_CASE("bravo peer: a callsign reaches the Go without moving the position's time") {
    Served served;
    served.seconds(2, true, nullptr);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, true, nullptr);
    REQUIRE(served.observations_of(kAircraft).size() == 1);
    const std::string position = served.observations_of(kAircraft)[0];
    CHECK(field(position, "callsign").empty());

    served.seconds(2, true, nullptr);
    served.rig.hear(adsb_identification(kAircraft, 5, 4, 1, "AFR123"));
    served.seconds(1, true, nullptr);
    const std::vector<std::string> observed = served.observations_of(kAircraft);
    REQUIRE(observed.size() == 2);
    CHECK(field(observed[1], "callsign") == "AFR123");
    CHECK(field(observed[1], "callsign_heard_s") != "");
    CHECK(field(observed[1], "at_s") == field(position, "at_s"));
    CHECK(field(observed[1], "latitude_1e7") == field(position, "latitude_1e7"));
    CHECK(field(observed[1], "sequence") == "1");
}

TEST_CASE("bravo peer: at most two frames a second for each target, the newest folded in") {
    Served served;
    served.seconds(2, true, nullptr);
    served.frames.clear();
    served.rig.push_fix();
    for (int i = 0; i < 10; i++) {
        served.rig.hear_aircraft(kAircraft, 2000 + 50 * i);
        served.run_for(80);
    }
    served.run_for(600);
    const std::vector<std::string> observed = served.observations_of(kAircraft);
    CHECK(observed.size() <= 3);
    CHECK(observed.size() >= 2);
    const std::string newest =
        std::to_string(served.rig.state().traffic.at(0)->observation.latitude_1e7);
    CHECK(field(observed.back(), "latitude_1e7") == newest);
}

TEST_CASE(
    "bravo peer: every Go that speaks peer gets the observations, only its own Go is paired") {
    Served served;
    PairedLink stranger{served.rig.platform.link(), 2, kStranger};
    served.stranger = &stranger;
    REQUIRE(stranger.connect());
    REQUIRE(stranger.write(served.claim_json(true, nullptr)) == Status::Ok);
    served.seconds(2, true, nullptr);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.seconds(1, true, nullptr);

    std::vector<std::string> theirs;
    for (std::string frame; stranger.take(frame);) theirs.push_back(link_text(frame));
    int paired = 0, observation = 0;
    for (const std::string& frame : theirs) {
        paired += field(frame, "command") == "health" && field(frame, "paired") == "true";
        observation += field(frame, "command") == "observation";
    }
    CHECK(paired == 0);
    CHECK(observation == 1);
    CHECK(served.observations_of(kAircraft).size() == 1);
}

TEST_CASE("bravo peer: the pinned Go's own ICAO drops its transponder, else Bravo's setting") {
    Served served;
    served.rig.product.settings().own_icao = kOtherAircraft;
    served.seconds(2, true, nullptr);
    REQUIRE(served.go.write(served.claim_json(true, nullptr, kAircraft)) == Status::Ok);
    served.run_for(100);
    served.rig.hear_aircraft(kAircraft, 2000);
    served.rig.hear_aircraft(kOtherAircraft, 2500);
    served.rig.push_fix();
    served.claim(nullptr, kAircraft);
    served.run_for(1000);
    CHECK(served.observations_of(kAircraft).empty());
    CHECK(served.observations_of(kOtherAircraft).size() == 1);
    CHECK(field(served.last_health(), "own_icao_set") == "true");
}

// The Go proposes this address as its own ICAO, so it must outlive the filter it then sets.
TEST_CASE("bravo peer: health names the transponder Bravo hears loudest, filtered or not") {
    Served served;
    const auto cockpit_second = [&served](uint32_t own_icao) {
        constexpr int8_t kCockpitDbm = -12;
        const double latitude = BravoRig::kLatitude1e7 / 1e7;
        const double longitude = BravoRig::kLongitude1e7 / 1e7;
        served.rig.hear(adsb_identification(kAircraft, 0, 4, 1, "FJABC"), kCockpitDbm);
        for (const bool odd : {false, true, false, true})
            served.rig.hear(adsb_airborne_position(kAircraft, 0, latitude, longitude, odd, 1500),
                            kCockpitDbm);
        served.rig.push_fix();
        served.claim(nullptr, own_icao);
        served.run_for(1000);
        served.rig.utc_offset_s++;
    };
    for (int second = 0; second < 3; second++) cockpit_second(0);
    CHECK(field(served.last_health(), "own_transponder").empty());

    for (uint32_t second = 0; second * 1000 <= bravo::OwnTransponder::kLeadHoldMs; second++)
        cockpit_second(0);
    CHECK(field(served.last_health(), "own_transponder") == std::to_string(kAircraft));
    CHECK(field(served.last_health(), "own_transponder_callsign") == "FJABC");
    CHECK(field(served.last_health(), "own_transponder_category") == "1");

    for (int second = 0; second < 3; second++) cockpit_second(kAircraft);
    CHECK(field(served.last_health(), "own_transponder") == std::to_string(kAircraft));
}

// The Go stores an own ICAO the pilot confirmed, and the Bravo in the same aircraft keeps it too.
TEST_CASE(
    "bravo peer: Bravo stores the own ICAO its pinned Go gives, and drops it when the Go does") {
    Served served;
    served.seconds(1, true, nullptr);
    REQUIRE(served.rig.product.settings().own_icao == bravo::kOwnIcaoUnset);
    served.rig.push_fix();
    served.claim(nullptr, kAircraft);
    served.run_for(1000);
    CHECK(served.rig.product.settings().own_icao == kAircraft);
    CHECK(field(served.last_health(), "own_icao_set") == "true");

    // A Go cleared on its glass sends none, and Bravo drops the address that Go gave it.
    served.seconds(2, true, nullptr);
    CHECK(served.rig.product.settings().own_icao == bravo::kOwnIcaoUnset);
    CHECK(field(served.last_health(), "own_icao_set") == "false");
}

TEST_CASE("bravo peer: a Go with no own ICAO leaves the one a phone set on Bravo") {
    Served served;
    served.rig.product.settings().own_icao = kOtherAircraft;
    served.seconds(3, true, nullptr);
    CHECK(served.rig.product.settings().own_icao == kOtherAircraft);
    CHECK(field(served.last_health(), "own_icao_set") == "true");
}

TEST_CASE("bravo peer: a stranger's claim never sets the reference or the own ICAO") {
    Served served(false);
    PairedLink stranger{served.rig.platform.link(), 2, kStranger};
    served.stranger = &stranger;
    REQUIRE(stranger.connect());
    GoPosition far{};
    far.latitude_1e7 += 5000000;
    REQUIRE(stranger.write(served.claim_json(true, &far, kAircraft)) == Status::Ok);
    served.seconds(2, false, nullptr);
    CHECK(served.rig.product.gate().choose(served.rig.state().own, served.rig.now_ms).reference ==
          comms::ExportReference::None);
    CHECK(served.rig.product.gate().own_icao(0) == 0);
    CHECK(served.rig.product.settings().own_icao == bravo::kOwnIcaoUnset);
}

TEST_CASE("bravo peer: a malformed claim is refused and counted, and changes nothing") {
    Served served;
    REQUIRE(served.go.write(complete_registry_frame(
                "{\"command\":\"peer_claim\",\"boot_epoch\":7,\"latitude_1e7\":485000000,"
                "\"alarm_requested\":false,"
                "\"paired\":true}")) == Status::Ok);
    served.run_for(200);
    CHECK(served.rig.product.peer().refused() == 1);
    CHECK(served.rig.product.peer().paired());
}

TEST_CASE("bravo peer: the pinned Go keeps a connection that a third phone cannot take") {
    Served served;
    served.go.drop();
    served.run_for(100);
    platform::host::Link& link = served.rig.platform.link();
    link.raise_link(2);
    link.raise_link(3);
    CHECK(link.links() == 2);
    CHECK(link.admits_phone());
    link.raise_link(4);
    CHECK(link.links() == 2);
    CHECK(link.refused() == 1);
    REQUIRE(served.go.connect());
    CHECK(link.links() == 3);
    CHECK_FALSE(link.admits_phone());
}
