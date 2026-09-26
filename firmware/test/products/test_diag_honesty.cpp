// Four facts a replayed corpus would have read as true: the tap that gathers them decides here.
#include <vector>

#include "core/diag/payload.h"
#include "core/timing/slot.h"
#include "products/skyblip_go/services/ownship.h"
#include "products/skyblip_go/services/traffic.h"
#include "test/support/screen_rig.h"

namespace {

std::vector<diag::Record> drained(diag::Recorder& recorder) {
    std::vector<diag::Record> out;
    diag::Record record{};
    while (recorder.peek(record)) {
        out.push_back(record);
        recorder.commit();
    }
    return out;
}

template <class T>
bool last_of(const std::vector<diag::Record>& records, T& out) {
    bool found = false;
    for (const diag::Record& record : records) found = diag::read(record, out) || found;
    return found;
}

// One service over null roles, its state written by hand: no board re-deriving what a case says.
struct ServiceRig {
    platform::host::Clock clock;
    ports::NullRoles null;
    ports::Roles roles{
        clock,          null.rf,          null.link, null.display,         null.kv,
        null.log_flash, null.annunciator, null.dfu,  null.die_temperature, null.indicator,
        null.gnss};
    bus::Bus bus{};
    bus::State state{};
    diag::Recorder recorder{};
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
        fix.utc = kUtc + now_ms / 1000;
        fix.lat_1e7 = 485000000;
        fix.lon_1e7 = 85000000;
        fix.alt_mm = 300000;
        fix.alt_msl_mm = 300000;
        fix.hdop_e2 = 100;
        fix.vdop_e2 = 150;
        fix.sats = 9;
        fix.updates = ++updates;
        bus.gnss.push(fix);
        clock.set_millis(now_ms);
        ownship.tick(now_ms);
    }

    static constexpr uint32_t kUtc = 1785628800;
    uint32_t updates{0};
};

}  // namespace

// The gate the transmitter reads: a corpus that dates it a pass late misdates why a burst went out.
TEST_CASE("diag gnss: the settle gate recorded is this pass's, not the one before it") {
    ServiceRig rig;
    bool settled_before = false;
    bool crossed = false;

    for (uint32_t t = 1000; t <= 8000 && !crossed; t += 1000) {
        rig.solve(t);
        const std::vector<diag::Record> records = drained(rig.recorder);
        crossed = rig.state.own.tx_settled && !settled_before;
        settled_before = rig.state.own.tx_settled;
        if (!crossed) continue;

        diag::Gnss gnss{};
        REQUIRE(last_of(records, gnss));
        CHECK(gnss.tx_settled);
        diag::Flight flight{};
        REQUIRE(last_of(records, flight));
        CHECK(flight.tx_settled);
    }

    REQUIRE(crossed);
}

// The seconds a receiver drops while its PPS keeps ticking: the clock still dates them.
TEST_CASE("diag burst: a second the anchored clock dated is never recorded as boot-relative") {
    ServiceRig rig;
    rig.state.clock.pps_locked = true;
    rig.state.clock.utc_s = ServiceRig::kUtc;
    rig.state.own.utc_valid = false;
    rig.clock.set_micros(4000000);

    events::RfEvent event{};
    event.type = events::RfEventType::CrcError;
    event.band = model::Band::M;
    event.at_us = 4000000;
    rig.bus.rf.push(event);
    rig.traffic.tick(4000);

    radio::Entry burst{};
    REQUIRE(last_of(drained(rig.recorder), burst));
    CHECK(burst.event == radio::Event::BadCrc);
    CHECK(burst.at_s == ServiceRig::kUtc);
    CHECK(burst.utc);
}

TEST_CASE("diag pps: an edge lost past the field's ceiling reads as the ceiling, not as recent") {
    ServiceRig rig;
    rig.state.clock.pps_locked = false;
    rig.state.clock.ms_since_pps = timing::kPpsHoldoverMs + 10000;
    rig.clock.set_millis(1000);
    rig.ownship.tick(1000);

    diag::Pps pps{};
    REQUIRE(last_of(drained(rig.recorder), pps));
    CHECK(pps.since_edge_ms == 0xFFFF);
    CHECK(pps.since_edge_ms > timing::kPpsHoldoverMs);
}

// Recorded before the alarm navigated, the page is the one the pilot was taken off.
TEST_CASE("diag screen: the page recorded is the one the alarm put up, not the one it replaced") {
    Rig rig;
    uint32_t t = 0;
    rig.recorder.arm();
    rig.run_seconds(t, 3);
    rig.screen.next_page();
    rig.run_seconds(t, 2);
    REQUIRE(rig.screen.page() != go::Page::Radar);
    drained(rig.recorder);

    rig.alarm(go::ScreenService::kAlarmTakesGlass);
    rig.tick(t += 1000);

    diag::Screen screen{};
    REQUIRE(last_of(drained(rig.recorder), screen));
    CHECK(screen.page == static_cast<uint8_t>(go::Page::Radar));
    CHECK(screen.page == static_cast<uint8_t>(rig.screen.page()));
    CHECK(screen.alarm == go::ScreenService::kAlarmTakesGlass);
}
