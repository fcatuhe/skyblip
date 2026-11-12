// A whole Go+ takes the alarm from a whole Bravo, which keeps silent only while the Go+ sounds
// (#179).
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/paired_products.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

using Direction = PairedLink::Direction;
using Flow = PairedLink::Flow;

constexpr uint32_t kAircraft = 0x3C6586;
constexpr int32_t kSpeedMmS = 25000;
// INFO: fc 12nov26 PairedProducts::run_for(0) is one pass of the Go, and one of Bravo with it
constexpr uint32_t kGoPassMs = 50;
// INFO: fc 12nov26 both barometers read sea level, so an aircraft at 100 ft is level with both
constexpr int32_t kNearSeaLevelFt = 100;

struct Flight {
    PairedProducts pair;
    bool go_fix{true};

    explicit Flight(ports::Capabilities go_fitted = platform::host::Platform::kFullyFitted)
        : pair(go_fitted) {
        pair.bravo.platform.barometer().set_altitude_mm(0);
        pair.pair_and_store();
        REQUIRE(pair.go_pinned());
        REQUIRE(pair.peer().paired());
    }

    // The Go's receiver where Bravo's is, both flying north at the same speed.
    void push_go_fix() {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.utc_valid = true;
        solution.utc_s = Rig::kBaseUtcS + pair.go.utc_offset_s;
        solution.latitude_1e7 = BravoRig::kLatitude1e7;
        solution.longitude_1e7 = BravoRig::kLongitude1e7;
        solution.altitude_msl_mm = 0;
        solution.altitude_hae_mm = gnss::kDefaultGeoidSeparationMm;
        solution.geoid_measured = true;
        solution.speed_mm_s = kSpeedMmS;
        solution.satellites = 10;
        solution.hdop_e2 = 100;
        solution.vdop_e2 = 150;
        solution.sentences = ++pair.go.fix_updates;
        pair.go.product.bus().gnss.push(solution);
    }

    void push_fixes() {
        if (go_fix) push_go_fix();
        pair.bravo.push_fix(kSpeedMmS);
        pair.go.utc_offset_s++;
        pair.bravo.utc_offset_s++;
    }

    void second() {
        push_fixes();
        pair.run_for(950);
    }

    void seconds(uint32_t count) {
        for (uint32_t i = 0; i < count; i++) second();
    }

    std::string last_claim() {
        const auto& sent = pair.go_wrote;
        for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
            const std::string text = link_text(*it);
            if (field(text, "command") == "peer_claim") return text;
        }
        return std::string();
    }

    std::string last_health() {
        const auto& sent = pair.bravo.platform.link().sent;
        for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
            if (it->endpoint != events::Endpoint::Peer) continue;
            const std::string text = link_text(it->bytes);
            if (field(text, "command") == "health") return text;
        }
        return std::string();
    }

    bool requested() {
        const std::string claim = last_claim();
        REQUIRE_FALSE(claim.empty());
        return field(claim, "alarm_requested") == "true";
    }
    bool granted() { return pair.bravo.state().alarm_granted; }
    uint32_t go_tones() { return pair.go.platform.annunciator().tone_commands(); }
    uint32_t bravo_tones() { return pair.bravo.platform.annunciator().tone_commands(); }
};

}  // namespace

TEST_CASE("bravo alarm: a Go+ with a fix takes the alarm from its live Bravo, which says so") {
    Flight flight;
    flight.seconds(4);
    CHECK(flight.pair.go.state().bravo == comms::BravoState::Live);
    CHECK(flight.requested());
    CHECK(flight.granted());
    CHECK(field(flight.last_health(), "alarm_granted") == "true");
}

TEST_CASE("bravo alarm: a Go+ without a fix never takes the alarm") {
    Flight flight;
    flight.go_fix = false;
    flight.seconds(4);
    CHECK(flight.pair.go.state().bravo == comms::BravoState::Live);
    CHECK_FALSE(flight.requested());
    CHECK_FALSE(flight.granted());
}

TEST_CASE("bravo alarm: a Go with no buzzer and no motor never takes the alarm") {
    Flight flight(ports::without(platform::host::Platform::kFullyFitted,
                                 ports::Capability::Buzzer | ports::Capability::Haptic));
    flight.seconds(4);
    CHECK(flight.pair.go.state().bravo == comms::BravoState::Live);
    CHECK_FALSE(flight.requested());
    CHECK_FALSE(flight.granted());
}

// Bravo's frames lost on the way to the Go, its claims still arriving: the Go+ has no Bravo
// traffic.
TEST_CASE("bravo alarm: a Go+ that loses its Bravo's health gives the alarm back") {
    Flight flight;
    flight.seconds(4);
    REQUIRE(flight.granted());

    constexpr uint32_t kSilenceCeilingMs =
        go::BravoService::kHealthMaxAgeMs + go::BravoService::kClaimPeriodMs + kGoPassMs;
    const uint32_t cut_ms = flight.pair.now_ms;
    flight.pair.air.set_flow(Direction::ToCentral, Flow::Lost);
    uint32_t next_fix_ms = cut_ms;
    while (flight.granted() && flight.pair.now_ms - cut_ms <= 2 * kSilenceCeilingMs) {
        if (flight.pair.now_ms >= next_fix_ms) {
            flight.push_fixes();
            next_fix_ms += 1000;
        }
        flight.pair.run_for(0);
    }
    CHECK(flight.pair.now_ms - cut_ms <= kSilenceCeilingMs);
    CHECK(comms::lost(flight.pair.go.state().bravo));
    CHECK_FALSE(flight.requested());
    CHECK_FALSE(flight.granted());
}

TEST_CASE("bravo alarm: off on the Go+ still keeps Bravo silent") {
    SUBCASE("volume off") {
        Flight flight;
        flight.pair.go.settings().alarm_volume = go::AlarmVolume::Off;
        flight.seconds(4);
        CHECK(flight.requested());
        CHECK(flight.granted());
    }
    SUBCASE("alarm off") {
        Flight flight;
        flight.pair.go.settings().alarm_enabled = false;
        flight.seconds(4);
        CHECK(flight.requested());
        CHECK(flight.granted());
    }
}

TEST_CASE("bravo alarm: the Go+ sounds for Bravo's traffic while Bravo keeps silent") {
    Flight flight;
    flight.seconds(20);
    REQUIRE(flight.granted());
    const uint32_t go_tones = flight.go_tones();
    const uint32_t bravo_tones = flight.bravo_tones();

    flight.pair.bravo.hear_aircraft(kAircraft, 800, kNearSeaLevelFt);
    flight.seconds(2);
    CHECK(flight.pair.bravo.state().alarm_undismissed == traffic::Level::Advisory);
    CHECK(flight.pair.go.state().alarm_undismissed == traffic::Level::Advisory);
    CHECK(flight.go_tones() > go_tones);
    CHECK(flight.bravo_tones() == bravo_tones);
    CHECK_FALSE(flight.pair.go.state().alarm_granted);
}
