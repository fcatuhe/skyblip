// End-to-end tests through the simulator: modelled GNSS emits real NMEA that the
// production parser decodes, and virtual aircraft are encoded as real ADS-L
// frames that the production receive path decodes into the traffic table and the
// collision alarm. No mocks of logic.
#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/events/link.h"
#include "core/flight/atmosphere.h"
#include "core/model/aircraft.h"
#include "core/units/units.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "products/skyblip_go/pages/recovery.h"
#include "simulator/simulator.h"

using namespace skyblip;

namespace {

void run(simulator::Simulator& h, uint32_t from, uint32_t to) {
    for (uint32_t t = from; t <= to; t += simulator::Simulator::kStepMs) h.step(t);
}

// A press has to outlast the board's debounce window, held across steps as a thumb would.
uint32_t press(simulator::Simulator& h, uint32_t t) {
    h.world().press_button();
    for (int i = 0; i < 5; i++) {
        h.step(t);
        t += 40;
    }
    return t;
}

// A touch has to outlast the pad's settle and end well inside go::Controls::kLongTouchMs.
uint32_t page(simulator::Simulator& h, uint32_t t) {
    h.world().tap_pad();
    for (int i = 0; i < 5; i++) {
        h.step(t);
        t += 40;
    }
    return t;
}

// Reached the way a thumb reaches it: the pad along the walk, then the menu row.
uint32_t show_sats(simulator::Simulator& h, uint32_t t) {
    for (int i = 0; i < go::kPageCount && h.product().screen().page() != go::Page::Nearby; i++)
        t = page(h, t);
    t = press(h, t);
    const go::Menu menu = go::menu_for(go::Page::Nearby);
    for (int i = 0;
         i < menu.n && go::page_behind(h.product().screen().editor().focus()) != go::Page::Sats;
         i++)
        t = page(h, t);
    return press(h, t);
}

bool glass_is(const ui::Canvas& glass, const ui::Canvas& expected) {
    for (int y = 0; y < go::kGlassH; y++)
        for (int x = 0; x < go::kGlassW; x++)
            if (glass.get_pixel(x, y) != expected.get_pixel(x, y)) return false;
    return true;
}

}  // namespace

TEST_CASE("simulator: simulated GNSS drives own-ship state via the real NMEA parser") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_sats(11);
    h.world().set_altitude_m(1500);
    h.world().set_speed_kt(50);
    h.world().set_track_deg(90);
    h.world().gnss().lat_1e7 = 485000000;
    h.world().gnss().lon_1e7 = 85000000;

    run(h, 0, 3000);

    CHECK(h.product().state().own.fix_valid);
    CHECK(h.product().state().own.utc_valid);
    CHECK(int(h.product().state().own.sats) == 11);
    CHECK(h.product().state().own.alt_mm == 1500000);
    CHECK(h.product().state().own.track_cdeg == 9000);
    CHECK(h.product().state().own.speed_mm_s > 25000);  // 50 kt is 25.7 m/s
    CHECK(h.product().state().own.lat_1e7 > 484000000);
    CHECK(h.product().state().own.lon_1e7 > 85000000);  // moved east on track 090
}

TEST_CASE("simulator: losing the fix clears own-ship validity (fail closed)") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);

    h.world().set_fix(false);
    run(h, 2000, 5000);
    CHECK_FALSE(h.product().state().own.fix_valid);
}

TEST_CASE("simulator: a virtual aircraft arrives as a real ADS-L frame and enters traffic") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    CHECK(h.product().state().traffic.count() == 0);

    h.world().add_aircraft(2000, 0, 0);  // 2 km north
    // A neighbour transmits once a second, and we are deaf through our own
    // burst, so the window has to be wider than one of its transmissions.
    run(h, 2000, 6000);

    CHECK(h.product().state().air.rx_ok > 0);         // frames actually decoded (CRC ok)
    CHECK(h.product().state().air.rx_bad == 0);       // and none corrupt
    CHECK(h.product().state().traffic.count() >= 1);  // fused into the table
}

// The one path a name can reach the glass by, end to end: a neighbour's own burst.
TEST_CASE("simulator: an aircraft that names itself is named on the device") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    const int index = h.world().add_aircraft(2000, 0, 0);
    REQUIRE(index >= 0);
    h.world().name_aircraft(index, "D-KXYZ");
    const uint32_t addr = h.world().aircraft_at(index)->addr;

    // A name goes out once in ten seconds, so the window has to be wider than that.
    run(h, 2000, 26000);

    CHECK(h.product().state().air.rx_named > 0);
    REQUIRE(h.product().state().callsigns.find(6, addr) != nullptr);
    CHECK(std::string(h.product().state().callsigns.find(6, addr)) == "D-KXYZ");
}

// The radar's chevron and its counting altitude tag have nothing to read until an aircraft moves.
TEST_CASE("simulator: a climbing aircraft broadcasts its rate and gains height as it flies") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);

    h.world().add_aircraft(2000, 0, 0, 30, 180, -1, -1, protocol::System::AdslDirect, 0, 3);
    run(h, 2000, 8000);

    const traffic::Target* target = nullptr;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = h.product().state().traffic.at(i);
        if (t != nullptr && t->used) target = t;
    }
    REQUIRE(target != nullptr);
    CHECK(target->obs.climb_valid);
    // 3 m/s is 24 eighths, and 590 fpm, which is past the chevron's 500.
    CHECK(int(target->obs.climb_e8) == 24);

    const int32_t was = target->obs.alt_m;
    run(h, 8000, 18000);
    CHECK(target->obs.alt_m > was + 20);
    CHECK(h.world().aircraft_at(0)->up_m > 40);
}

// A rate on air that the aeroplane does not fly is a simulator lying to the extrapolator.
TEST_CASE("simulator: traffic reporting no climb holds its level while own ship climbs past") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().add_aircraft(2000, 0, 0);
    run(h, 0, 6000);

    const traffic::Target* target = h.product().state().traffic.at(0);
    REQUIRE(target != nullptr);
    const int32_t level_at = target->obs.alt_m;
    const int32_t own_at = to_metres(Millimetres(h.product().state().own.alt_mm)).v;

    h.world().set_climb_mm_s(5000);
    run(h, 6000, 16000);

    // Own ship gains 50 m; the target holds its level within the second of own-ship lag.
    CHECK(to_metres(Millimetres(h.product().state().own.alt_mm)).v > own_at + 40);
    CHECK(target->obs.alt_m < level_at + 10);
    CHECK(target->obs.alt_m > level_at - 10);
}

TEST_CASE("simulator: a converging aircraft raises the collision alarm and buzzer") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    CHECK(int(h.buzzer_level()) == 0);

    h.world().add_threat();  // ~600 m, converging, sinking through our level
    run(h, 2000, 6000);

    CHECK(h.product().state().traffic.count() >= 1);
    // An alarm is a pattern, so the buzzer is off as often as it is on: what is
    // being announced is the thing that stands, and the instantaneous pitch
    // step is only ever true inside a beep.
    CHECK(int(h.announcing_level()) > 0);  // annunciator was driven
}

TEST_CASE("simulator: clearing traffic empties the table and silences the alarm") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    h.world().add_threat();
    run(h, 2000, 6000);
    REQUIRE(h.product().state().traffic.count() >= 1);

    // Nothing transmits any more, so the targets age out of the table on their
    // own schedule (core/traffic 30 s) rather than being deleted behind its back.
    h.world().clear_aircraft();
    run(h, 6000, 42000);
    CHECK(h.product().state().traffic.count() == 0);
    CHECK(int(h.buzzer_level()) == 0);
}

TEST_CASE("simulator: every page renders ink to the panel") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().add_aircraft(1500, 500, 50);
    run(h, 0, 2500);

    uint32_t t = 2500;
    for (int i = 0; i < static_cast<int>(go::Page::kCount); i++) {
        run(h, t, t + 1000);
        t += 1000;
        CHECK(h.panel().count_black() > 20);  // something was drawn
        t = page(h, t);
    }
    CHECK(h.present_count() > 0);
}

TEST_CASE("simulator: a modelled turn deflects the six-pack turn coordinator") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(100);
    h.world().set_track_deg(0);
    run(h, 0, 2000);
    uint32_t page_t = 2000;
    page_t = page(h, page_t);  // radar -> nearby
    page_t = page(h, page_t);  // nearby -> 6-pack
    run(h, page_t, page_t + 2000);
    REQUIRE(h.product().screen().page() == go::Page::SixPack);
    const parts::Ssd1681Glass level = h.panel();

    // A standard-rate turn: 3 deg/s of track change, held four seconds.
    uint32_t t = page_t + 2000;
    for (int i = 1; i <= 4; i++) {
        h.world().set_track_deg(i * 3);
        run(h, t, t + 1000);
        t += 1000;
    }

    int diff = 0;  // the turn tile: centre (34, 138), radius 29
    for (int y = 109; y <= 167; y++)
        for (int x = 5; x <= 63; x++)
            if (level.get_pixel(x, y) != h.panel().get_pixel(x, y)) diff++;
    CHECK(diff > 10);
}

TEST_CASE("simulator: a modelled climb reaches own-ship state through the barometer") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_altitude_m(1000);
    h.world().set_climb_mm_s(3000);  // +3.0 m/s, integrated by the GNSS model's altitude
    run(h, 0, 6000);

    // Both sensors see the same air, and the barometer is what publishes the rate.
    CHECK(h.product().state().own.climb_mm_s > 2000);
    CHECK(h.product().state().own.climb_mm_s < 4000);
    // above sea level
    CHECK(h.world().baro().pressure_mpa() < flight::kIsaSeaLevelPa * 1000);

    h.world().set_climb_mm_s(-3000);  // now sinking
    run(h, 6000, 14000);
    CHECK(h.product().state().own.climb_mm_s < 0);
}

// The pinned bug: instruments fed from the receiver's 1 Hz whole-degree, whole-metre report.
TEST_CASE("simulator: a turn and a climb held steady are published steady") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_turn_dps(3.0);
    h.world().set_climb_mm_s(1524);  // 300 fpm, what the page's slider asks for
    h.world().set_speed_kt(60);
    run(h, 0, 10000);

    int32_t worst_turn = 0;
    int32_t worst_vs = 0;
    int32_t worst_kt = 0;
    for (uint32_t t = 10000; t <= 30000; t += simulator::Simulator::kStepMs) {
        h.step(t);
        const model::OwnState& own = h.product().state().own;
        worst_turn = std::max(worst_turn, std::abs(own.turn_cdps - 300));
        worst_vs = std::max(
            worst_vs, std::abs(to_feet_per_minute(MillimetresPerSec(own.climb_mm_s)).v - 300));
        worst_kt = std::max(worst_kt, std::abs(to_knots(MillimetresPerSec(own.speed_mm_s)).v - 60));
    }
    CHECK(worst_turn <= 10);
    CHECK(worst_vs <= 5);
    CHECK(worst_kt == 0);
}

TEST_CASE("simulator: an aircraft entering the window buzzes and vibrates") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);
    REQUIRE(h.haptic_ms() == 0);

    // Nothing in the window yet: nothing said, nothing felt.
    h.world().add_aircraft(6000, 0, 0, 20, 90);
    run(h, 2000, 5000);
    REQUIRE(h.announcing_level() == traffic::Level::None);
    CHECK(h.haptic_ms() == 0);

    // Now one inside it: said out loud, and felt through the harness.
    h.world().add_threat();
    run(h, 5000, 9000);
    REQUIRE(h.announcing_level() == traffic::Level::Advisory);
    CHECK(h.haptic_ms() >= 200);
}

TEST_CASE("simulator: a threat going away does not buzz the motor again") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    h.world().add_threat();
    uint8_t loudest = 0;
    for (uint32_t t = 2000; t <= 6000; t += simulator::Simulator::kStepMs) {
        h.step(t);
        if (h.buzzer_level() > loudest) loudest = h.buzzer_level();
    }
    REQUIRE(loudest >= 1);
    REQUIRE(h.haptic_ms() >= 200);

    // The sky emptying is a level CHANGE too, and it must not be mistaken for a
    // new contact: the annunciator records the last duration, so a fresh pulse
    // would show up as a change here.
    const uint16_t after_escalation = h.haptic_ms();
    h.world().clear_aircraft();
    run(h, 6000, 40000);
    CHECK(h.buzzer_level() == 0);
    CHECK(h.haptic_ms() == after_escalation);  // unchanged: no pulse on the way down
}

// The world can connect a phone and take it away again, which is the seam this
// tree did not have: before it, no host code and no board could raise a link at
// all, so the unsolicited gauge push could only ever be exercised by calling the
// service by hand and was dead on a real device.
TEST_CASE("simulator: a phone connects, the gauge pushes, the phone leaves and it stops") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_battery_mv(4050);
    run(h, 0, 4000);
    REQUIRE_FALSE(h.companion_connected());

    h.world().connect_companion();
    run(h, 4000, 4200);
    REQUIRE(h.companion_connected());
    const int before = h.companion_frames(events::Endpoint::Config);

    h.world().set_battery_mv(3600);
    run(h, 4200, 9000);
    const int pushed = h.companion_frames(events::Endpoint::Config);
    CHECK(pushed > before);

    h.world().disconnect_companion();
    run(h, 9000, 9200);
    REQUIRE_FALSE(h.companion_connected());
    h.world().set_external_power(true);
    h.world().set_battery_mv(4100);
    run(h, 9200, 15000);
    CHECK(h.companion_frames(events::Endpoint::Config) == pushed);
}

// The same call the page's "+ Aircraft" button and the terminal's [j] make. If
// this passes and the page still shows nothing, the gap is in the shell, not in
// the receive path.
TEST_CASE("simulator: an ALP-TAS-equipped aircraft enters traffic as ALP-TAS") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);
    REQUIRE(h.product().state().own.fix_valid);

    h.world().add_aircraft(2000, 0, 0, 30, 270, -1, -1, protocol::System::Alptas);
    run(h, 2000, 5000);

    const traffic::TrafficTable& table = h.product().state().traffic;
    REQUIRE(table.count() >= 1);
    int alptas = 0;
    for (int i = 0; i < traffic::TrafficTable::kCapacity; i++) {
        const traffic::Target* t = table.at(i);
        if (t != nullptr && t->used && t->obs.source == model::Source::Alptas) alptas++;
    }
    CHECK(alptas == 1);
}

// The browser page drives the hold through world::hold_button, and a tap cannot
// stand in for it: kPressMs is 60 ms and the device switches off at 2 s.
TEST_CASE("simulator: holding the button switches the device off, a tap never does") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    run(h, 0, 2000);

    uint32_t t = press(h, 2000);
    run(h, t, t + power::kLongPressMs);
    CHECK(h.product().shutdown().phase() == power::ShutdownPhase::Running);

    t += power::kLongPressMs;
    h.world().hold_button(true);
    run(h, t, t + power::kLongPressMs + 100);
    t += power::kLongPressMs + 100;
    CHECK(h.product().shutdown().reason() == power::ShutdownReason::LongPress);

    h.world().hold_button(false);
    run(h, t, t + power::kParkMs + power::kReleaseSettleMs + 100);
    CHECK(h.product().ready_to_power_off());
    CHECK_FALSE(h.panel_powered());
}

// Nothing but the satellites page reads a level, and a GSV set is 648 bytes of a 9600 baud second.
TEST_CASE("simulator: satellites in view are asked for by the page that draws them") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(false);
    run(h, 0, 6000);

    CHECK_FALSE(h.world().gnss().gsv_enabled());
    CHECK_FALSE(h.product().state().gnss.levels_live);
    CHECK(h.product().state().gnss.sky.count() == 0);

    uint32_t t = show_sats(h, 6000);
    run(h, t, t + 3000);
    REQUIRE(h.product().screen().page() == go::Page::Sats);
    CHECK(h.world().gnss().gsv_enabled());
    CHECK(h.product().state().gnss.levels_live);
    CHECK(h.product().state().gnss.sky.count() > 0);
    CHECK(h.product().state().gnss.sky.in_use() == 0);
    CHECK(h.product().state().gnss.stage != gnss::Stage::Fixed);

    t = page(h, t + 3000);
    run(h, t, t + 3000);
    REQUIRE(h.product().screen().page() != go::Page::Sats);
    CHECK_FALSE(h.world().gnss().gsv_enabled());
    CHECK_FALSE(h.product().state().gnss.levels_live);
    CHECK(h.product().state().gnss.sky.count() == 0);

    t = show_sats(h, t + 3000);
    h.world().set_fix(true);
    run(h, t, t + 20000);
    REQUIRE(h.product().state().own.fix_valid);
    REQUIRE(h.product().state().own.tx_settled);

    CHECK(h.world().gnss().gsv_enabled());
    CHECK(h.product().state().gnss.levels_live);
    CHECK(h.product().state().gnss.sky.in_use() > 0);
    CHECK(h.product().state().gnss.sky.in_use_of(gnss::System::Gps) > 0);
    CHECK(h.product().state().gnss.sky.in_use_of(gnss::System::Beidou) > 0);
}

// The board arms its watchdog before a pilot can ask, so a real recovery waits for the press.
TEST_CASE("simulator: a confirmed recovery asks for the press, as the board does") {
    simulator::Simulator h;
    REQUIRE(h.setup() == Status::Ok);
    h.world().set_fix(true);
    h.world().set_speed_kt(0);
    run(h, 0, 20000);
    h.world().connect_companion();
    run(h, 20000, 20200);
    h.world().send_config("{\"cmd\":\"recovery\"}");
    run(h, 20200, 20400);
    REQUIRE(h.product().config().config().pending() == comms::Pending::Recovery);

    h.product().config().config().confirm();
    run(h, 20400, 20400 + power::kParkMs + power::kReleaseSettleMs + 5000);

    go::Glass expected;
    go::draw_recovery(expected, ports::RecoveryPath::PowerOffToFinish);
    CHECK(glass_is(h.panel(), expected));
    CHECK(h.product().ready_to_power_off());
}
