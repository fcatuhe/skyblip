// A flight run: the glass flies while the unit sits on the bench, and nothing false goes on air.
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "core/protocol/adsl.h"
#include "core/protocol/air.h"
#include "core/timing/slot.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/settings.h"
#include "simulator/simulator.h"
#include "test/support/diag_taps.h"
#include "test/support/glass_text.h"
#include "test/support/product_rig.h"
#include "test/support/rig_moves.h"
#include "test/support/simulator_run.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAirWindowMs = 30000;

struct Said {
    int positions{0};
    int names{0};
};

Said said_on_air(const simulator::Air& air) {
    Said said{};
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx) continue;
        if (timing::Scheduler::in_direct_slot(r.phase_ms))
            said.positions++;
        else
            said.names++;
    }
    return said;
}

void park_on_the_bench(simulator::Simulator& h) {
    std::strncpy(h.product().settings().callsign, "D-KXYZ", go::kCallsignCap - 1);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    h.world().set_altitude_m(300);
}

uint32_t arm_a_flight_run(simulator::Simulator& h) {
    const uint32_t armed_ms = past_settling(h);
    h.product().diag().arm(diag::Profile::FlightRun);
    run_on(h, armed_ms, 1000);
    REQUIRE(h.product().capture().capturing());
    h.world().air().clear();
    return armed_ms + 1000;
}

void arm_flight_run(Rig& rig, uint32_t& t) { arm(rig, t, diag::Profile::FlightRun); }

const go::Glass& glass(Rig& rig) { return rig.product.screen().framebuffer(); }

}  // namespace

// G.1.16: on ground the rate shall be 0.1 Hz, so the glass flies and the transmitter does not.
TEST_CASE("flight run: a parked unit keeps the ground cadence on air while the glass flies") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    park_on_the_bench(h);
    const uint32_t from_ms = arm_a_flight_run(h);
    run_on(h, from_ms, kAirWindowMs);

    CHECK(h.product().state().flight.running);
    const Said said = said_on_air(h.world().air());
    // a position and a name every ten seconds, at most one more for the window's two ends
    CHECK(said.positions >= 3);
    CHECK(said.positions <= 4);
    CHECK(said.names >= 3);
    CHECK(said.names <= 4);
}

TEST_CASE("flight run: every burst on air says on ground, at the real position, standing still") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    park_on_the_bench(h);
    const uint32_t from_ms = arm_a_flight_run(h);
    run_on(h, from_ms, kAirWindowMs);
    REQUIRE(h.product().state().flight.running);

    const model::OwnState& own = h.product().state().own;
    const simulator::Air& air = h.world().air();
    int checked = 0;
    for (int i = 0; i < air.record_count(); i++) {
        const simulator::AirRecord& r = air.record(i);
        if (r.event != simulator::AirEvent::Tx || !timing::Scheduler::in_direct_slot(r.phase_ms))
            continue;
        protocol::Frame frame{};
        REQUIRE(simulator::Air::framed(r, frame));
        protocol::AdslPacket p{};
        p.init();
        std::memcpy(&p.Version, frame.data, protocol::kAdslFrameBytes);
        REQUIRE(p.check_crc() == 0);
        p.descramble();
        CHECK(p.FlightState == static_cast<uint8_t>(flight::FlightState::Ground));
        // the payload carries latitude in steps of 107 and longitude of 215, in 1e-7 degrees
        CHECK(std::abs(p.lat_1e7() - own.lat_1e7) < 200);
        CHECK(std::abs(p.lon_1e7() - own.lon_1e7) < 400);
        CHECK(p.speed_q() == 0);
        checked++;
    }
    CHECK(checked >= 3);
}

TEST_CASE("flight run: records the pair every 30 s, and its boot names the profile") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_flight_run(rig, t);
    rig.run(t, t + 65000);
    t += 65000;
    stop(rig, t);
    const std::vector<diag::Record> records = captured(rig);

    CHECK(count_of(records, diag::Type::Power) == 2);
    CHECK(count_of(records, diag::Type::Duty) == 2);
    CHECK(count_of(records, diag::Type::Flight) == 0);
    diag::Boot boot{};
    REQUIRE(first_of(records, boot));
    CHECK(boot.profile_recorded);
    CHECK(boot.profile == diag::Profile::FlightRun);
    CHECK(rig.product.capture().keeps_s(diag::Profile::FlightRun) ==
          rig.product.capture().keeps_s(diag::Profile::PowerRun));
}

// A simulated flight in the pilot's log could not be told from a real one, so it writes none.
TEST_CASE("flight run: the radar flies, the flight log writes nothing, the gates stay grounded") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    REQUIRE(rig.product.screen().page() == go::Page::Radar);
    arm_flight_run(rig, t);
    taxi(rig, t, 65);

    CHECK(rig.state().flight.running);
    CHECK(rig.state().flight.seconds >= 60);
    CHECK(reads_in(glass(rig), "FLIGHT", 0, 168, 50, 182));
    CHECK(reads_in(glass(rig), "0:01", 0, 176, 60, 198, 2));
    CHECK_FALSE(reads_in(glass(rig), "GROUND", 0, 0, 200, 199, 2));

    CHECK_FALSE(rig.product.flight_log().recording());
    CHECK(rig.product.flight_log().sessions_on_flash() == 0);
    CHECK(rig.state().own.flight_state == static_cast<uint8_t>(flight::FlightState::Ground));
    CHECK(rig.state().flight.confirmed_state == flight::FlightState::Ground);
}

TEST_CASE("flight run: the glass refreshes as a flown one does, and a parked one does not") {
    Rig parked;
    REQUIRE(parked.setup() == Status::Ok);
    uint32_t pt = 100;
    taxi(parked, pt, 3);
    arm(parked, pt, diag::Profile::PowerRun);
    const uint32_t parked_from = parked.state().duty.panel_partial_refreshes;
    taxi(parked, pt, 130);

    Rig flying;
    REQUIRE(flying.setup() == Status::Ok);
    uint32_t ft = 100;
    taxi(flying, ft, 3);
    arm_flight_run(flying, ft);
    const uint32_t flying_from = flying.state().duty.panel_partial_refreshes;
    taxi(flying, ft, 130);

    // the clock turns 0:00, 0:01 and 0:02, and FLIGHT replaces GROUND once
    CHECK(flying.state().duty.panel_partial_refreshes - flying_from >= 2);
    CHECK(flying.state().duty.panel_partial_refreshes - flying_from >
          parked.state().duty.panel_partial_refreshes - parked_from);
}

TEST_CASE("flight run: a stop lands the glass, and leaves no flight time behind") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 3);
    arm_flight_run(rig, t);
    taxi(rig, t, 65);
    REQUIRE(rig.state().flight.running);

    stop(rig, t);
    taxi(rig, t, 2);
    CHECK_FALSE(rig.state().flight.running);
    CHECK_FALSE(rig.state().flight.time_valid);
    CHECK(rig.state().flight.seconds == 0);
    CHECK(reads_in(glass(rig), "GROUND", 40, 120, 160, 160, 2));
    CHECK(reads_in(glass(rig), "-:--", 0, 176, 60, 198, 2));
}

TEST_CASE("flight run: a power run and a full capture leave the glass on the ground") {
    for (const diag::Profile profile : {diag::Profile::Full, diag::Profile::PowerRun}) {
        Rig rig;
        REQUIRE(rig.setup() == Status::Ok);
        uint32_t t = 100;
        taxi(rig, t, 3);
        arm(rig, t, profile);
        taxi(rig, t, 5);
        CHECK_FALSE(rig.state().flight.running);
        CHECK(reads_in(glass(rig), "GROUND", 40, 120, 160, 160, 2));
    }
}
