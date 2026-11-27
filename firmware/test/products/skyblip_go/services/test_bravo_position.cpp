// A Go without a fix draws its traffic around its Bravo's fix, which is the same aircraft's.
#include <cstdint>

#include "core/comms/peer.h"
#include "core/model/aircraft.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "simulator/paired_simulator.h"
#include "test/support/glass_text.h"
#include "test/support/nearby_glass.h"
#include "test/support/radar_rig.h"

using namespace skyblip;

namespace {

struct PairedFlight {
    simulator::PairedSimulator simulator;
    uint32_t now_ms{0};

    PairedFlight() {
        REQUIRE(simulator.setup() == Status::Ok);
        REQUIRE(simulator.load_file("scenarios/paired.json"));
    }

    void run_until(uint32_t until_ms) {
        for (; now_ms <= until_ms; now_ms += simulator::PairedSimulator::kStepMs)
            simulator.step(now_ms);
    }

    const bus::State& go() { return simulator.product().state(); }
    const ui::Canvas& glass() { return simulator.product().screen().framebuffer(); }
    bool reads(const char* text, int scale = 1) {
        return reads_in(glass(), text, 0, 0, go::kGlassWidth, go::kGlassHeight, scale);
    }
};

}  // namespace

TEST_CASE("bravo position: a Go without a fix lists traffic around its Bravo's, saying BRAVO GPS") {
    PairedFlight flight;
    flight.run_until(10000);
    flight.simulator.world().set_fix(false);
    flight.run_until(16000);
    REQUIRE_FALSE(flight.go().own.fix_valid);
    REQUIRE(flight.go().bravo == comms::BravoState::Live);
    CHECK(flight.reads("BRAVO GPS"));

    flight.simulator.world().tap_pad();
    flight.run_until(18000);
    REQUIRE(flight.simulator.product().screen().page() == go::Page::Nearby);
    CHECK(go::nearby_lists(flight.glass(), 'B', "300002"));
}

// The pilot reads BRAVO GPS as a picture to look at: nothing the Go grades or sends rests on it.
TEST_CASE("bravo position: Bravo's fix raises no alarm and puts nothing on air") {
    PairedFlight flight;
    flight.run_until(10000);
    flight.simulator.world().set_fix(false);
    flight.run_until(16000);
    REQUIRE(flight.reads("BRAVO GPS"));
    CHECK(flight.go().alarm_level == traffic::Level::None);
    CHECK_FALSE(flight.go().transmit.on_air());
    CHECK_FALSE(flight.go().own_second.fix_valid);
}

TEST_CASE("bravo position: a Go whose fix comes back plots around its own again") {
    PairedFlight flight;
    flight.run_until(10000);
    flight.simulator.world().set_fix(false);
    flight.run_until(16000);
    REQUIRE(flight.reads("BRAVO GPS"));
    flight.simulator.world().set_fix(true);
    flight.run_until(20000);
    CHECK(flight.go().own.fix_valid);
    CHECK_FALSE(flight.reads("BRAVO GPS"));
}

TEST_CASE("bravo position: a Go whose Bravo has no fix either reads its own stage") {
    PairedFlight flight;
    flight.run_until(10000);
    flight.simulator.aboard().set_fix(false);
    flight.simulator.world().set_fix(false);
    flight.run_until(16000);
    CHECK_FALSE(flight.reads("BRAVO GPS"));
    CHECK(flight.reads("FINDS FIX"));
}

// A Bravo's own fix travels in its health, and a Go reads back what the Bravo wrote.
TEST_CASE("bravo position: health carries Bravo's fix, and a half position is refused whole") {
    comms::PeerHealth health{};
    health.boot_epoch = 7;
    health.listening = true;
    comms::BravoPosition position{};
    position.latitude_1e7 = -485000000;
    position.longitude_1e7 = 85000000;
    position.age_ms = 400;
    position.altitude_m = 1047;
    position.speed_q = 185;
    position.track_c9 = 128;
    health.position = position;
    uint8_t frame[comms::kPeerPayloadFloorBytes];
    const int bytes = comms::write_health(health, frame, sizeof(frame));
    REQUIRE(bytes > 0);
    const Result<comms::PeerHealth> read =
        comms::read_health(ConstByteSpan(frame, static_cast<size_t>(bytes)));
    REQUIRE(read.ok());
    REQUIRE(read.value().position);
    CHECK(read.value().position->latitude_1e7 == -485000000);
    CHECK(read.value().position->age_ms == 400);
    CHECK(read.value().position->altitude_m == 1047);
    CHECK(read.value().position->speed_q == 185);
    CHECK(read.value().position->track_c9 == 128);

    health.position->speed_q.reset();
    health.position->altitude_m.reset();
    const int narrower = comms::write_health(health, frame, sizeof(frame));
    const Result<comms::PeerHealth> plain =
        comms::read_health(ConstByteSpan(frame, static_cast<size_t>(narrower)));
    REQUIRE(plain.ok());
    CHECK_FALSE(plain.value().position->speed_q);
    CHECK_FALSE(plain.value().position->altitude_m);
}
