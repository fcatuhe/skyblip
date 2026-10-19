// Four facts a replayed corpus would have read as true: the tap that gathers them decides here.
#include <vector>

#include "core/diagnostics/payload.h"
#include "core/timing/slot.h"
#include "products/skyblip_go/services/ownship.h"
#include "products/skyblip_go/services/traffic.h"
#include "test/support/screen_rig.h"

namespace {

std::vector<diagnostics::Record> drained(diagnostics::Recorder& recorder) {
    std::vector<diagnostics::Record> out;
    diagnostics::Record record{};
    while (recorder.peek(record)) {
        out.push_back(record);
        recorder.commit();
    }
    return out;
}

template <class T>
bool last_of(const std::vector<diagnostics::Record>& records, T& out) {
    bool found = false;
    for (const diagnostics::Record& record : records)
        found = diagnostics::read(record, out) || found;
    return found;
}

// One service over null roles, its state written by hand: no board re-deriving what a case says.
struct ServiceRig {
    platform::host::Clock clock;
    platform::host::Identity identity{platform::host::Identity::kSerial};
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.key_value,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss,      identity};
    bus::Bus bus{};
    bus::State state{};
    diagnostics::Recorder recorder{};
    runtime::Context context{roles, bus, state, recorder};
    go::Settings settings{};
    go::OwnshipService ownship{context, settings};
    go::TrafficService traffic{context, go::Feature::None};

    ServiceRig() { recorder.arm(); }

    // A receiver holding station: each solution predicts the next exactly, so the settle converges.
    void solve(uint32_t now_ms) {
        gnss::GnssSolution fix{};
        fix.fix_valid = true;
        fix.utc_valid = true;
        fix.utc_s = kBaseUtcS + now_ms / 1000;
        fix.latitude_1e7 = 485000000;
        fix.longitude_1e7 = 85000000;
        fix.altitude_hae_mm = 300000;
        fix.altitude_msl_mm = 300000;
        fix.hdop_e2 = 100;
        fix.vdop_e2 = 150;
        fix.satellites = 9;
        fix.sentences = ++updates;
        bus.gnss.push(fix);
        clock.set_milliseconds(now_ms);
        ownship.tick(now_ms);
    }

    static constexpr uint32_t kBaseUtcS = 1785628800;
    uint32_t updates{0};
};

}  // namespace

// The gate the transmitter reads: a corpus that dates it a pass late misdates why a burst went out.
TEST_CASE("diagnostics gnss: the settle gate recorded is this pass's, not the one before it") {
    ServiceRig rig;
    bool settled_before = false;
    bool crossed = false;

    for (uint32_t now_ms = 1000; now_ms <= 8000 && !crossed; now_ms += 1000) {
        rig.solve(now_ms);
        const std::vector<diagnostics::Record> records = drained(rig.recorder);
        crossed = rig.state.own.tx_settled && !settled_before;
        settled_before = rig.state.own.tx_settled;
        if (!crossed) continue;

        diagnostics::Gnss gnss{};
        REQUIRE(last_of(records, gnss));
        CHECK(gnss.tx_settled);
        diagnostics::Flight flight{};
        REQUIRE(last_of(records, flight));
        CHECK(flight.tx_settled);
    }

    REQUIRE(crossed);
}

// The seconds a receiver drops while its PPS keeps ticking: the clock still dates them.
TEST_CASE(
    "diagnostics burst: a second the anchored clock dated is never recorded as boot-relative") {
    ServiceRig rig;
    rig.state.clock.pps_locked = true;
    rig.state.clock.utc_s = ServiceRig::kBaseUtcS;
    rig.state.own.utc_valid = false;
    rig.clock.set_microseconds(4000000);

    events::RfEvent event{};
    event.type = events::RfEventType::CrcError;
    event.band = model::Band::M;
    event.at_us = 4000000;
    rig.bus.rf.push(event);
    rig.traffic.tick(4000);

    radio::Entry burst{};
    REQUIRE(last_of(drained(rig.recorder), burst));
    CHECK(burst.verdict == radio::Verdict::BadCrc);
    CHECK(burst.at_s == ServiceRig::kBaseUtcS);
    CHECK(burst.utc_dated);
}

TEST_CASE(
    "diagnostics pps: an edge lost past the field's ceiling reads as the ceiling, not as recent") {
    ServiceRig rig;
    rig.state.clock.pps_locked = false;
    rig.state.clock.since_edge_ms = timing::kPpsHoldoverMs + 10000;
    rig.clock.set_milliseconds(1000);
    rig.ownship.tick(1000);

    diagnostics::Pps pps{};
    REQUIRE(last_of(drained(rig.recorder), pps));
    CHECK(pps.since_edge_ms == 0xFFFF);
    CHECK(pps.since_edge_ms > timing::kPpsHoldoverMs);
}

// Recorded before the alarm navigated, the page is the one the pilot was taken off.
TEST_CASE(
    "diagnostics screen: the page recorded is the one the alarm put up, not the one it replaced") {
    ScreenRig rig;
    uint32_t now_ms = 0;
    rig.recorder.arm();
    rig.run_seconds(now_ms, 3);
    rig.screen.next_page();
    rig.run_seconds(now_ms, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);
    drained(rig.recorder);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.tick(now_ms += 1000);

    diagnostics::Screen screen{};
    REQUIRE(last_of(drained(rig.recorder), screen));
    CHECK(screen.page == static_cast<uint8_t>(go::Page::Radar));
    CHECK(screen.page == static_cast<uint8_t>(rig.screen.page()));
    CHECK(screen.alarm == go::ScreenService::kAlarmTakesGlass);
}
