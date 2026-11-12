#include "simulator/bravo_simulator.h"
#include "simulator/simulator.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define KEEPALIVE
#endif

using namespace skyblip;

namespace {
simulator::Simulator g_simulator;
simulator::BravoSimulator g_bravo;
bool g_bravo_flying = false;

template <class Visit>
decltype(auto) on_unit(Visit visit) {
    return g_bravo_flying ? visit(g_bravo) : visit(g_simulator);
}

const bus::State& flying_state() {
    return on_unit([](auto& unit) -> const bus::State& { return unit.product().state(); });
}
}

extern "C" {

// INFO: fc 25sep26 a setup that failed draws the self-test page, which is the answer the page shows
KEEPALIVE void simulator_setup() {
    (void)g_simulator.setup();
    (void)g_bravo.setup();
}
KEEPALIVE void simulator_step(unsigned now_ms) {
    on_unit([&](auto& unit) { unit.step(now_ms); });
}
KEEPALIVE int simulator_park_refusal(unsigned now_ms) {
    return on_unit([&](auto& unit) { return unit.park_refusal(now_ms); }) ? 1 : 0;
}
KEEPALIVE int simulator_load_scenario(const char* json, int bytes) {
    simulator::Scenario scenario;
    if (!simulator::parse_scenario(json, bytes, scenario)) return 0;
    g_bravo_flying = scenario.product == simulator::Product::Bravo;
    on_unit([&](auto& unit) { unit.load(scenario); });
    return 1;
}
KEEPALIVE int simulator_mode() {
    return static_cast<int>(on_unit([](auto& unit) { return unit.mode(); }));
}
KEEPALIVE int simulator_product() {
    return static_cast<int>(g_bravo_flying ? simulator::Product::Bravo : simulator::Product::Go);
}

KEEPALIVE void simulator_press_button() {
    on_unit([](auto& unit) { unit.world().press_button(); });
}
KEEPALIVE void simulator_hold_button(int down) {
    on_unit([&](auto& unit) { unit.world().hold_button(down != 0); });
}
KEEPALIVE void simulator_hold_pad(int down) { g_simulator.world().hold_pad(down != 0); }
KEEPALIVE void simulator_set_backlight(int on) {
    g_simulator.product().screen().set_backlight(on != 0);
}
KEEPALIVE void simulator_set_panel_power(int on) {
    g_simulator.product().screen().set_power(on != 0);
}
KEEPALIVE void simulator_set_range(int step) {
    g_simulator.product().screen().set_range_step(step);
}

KEEPALIVE void simulator_set_fix(int on) {
    on_unit([&](auto& unit) { unit.world().set_fix(on != 0); });
}
KEEPALIVE void simulator_set_pps(int on) {
    on_unit([&](auto& unit) { unit.world().set_pps_locked(on != 0); });
}
KEEPALIVE void simulator_set_satellites(int satellites) {
    on_unit([&](auto& unit) { unit.world().set_satellites(satellites); });
}
KEEPALIVE void simulator_set_altitude_m(int altitude_m) {
    on_unit([&](auto& unit) { unit.world().set_altitude_m(altitude_m); });
}
KEEPALIVE void simulator_set_speed_kt(int speed_kt) {
    on_unit([&](auto& unit) { unit.world().set_speed_kt(speed_kt); });
}
KEEPALIVE void simulator_set_track_deg(int track_deg) {
    on_unit([&](auto& unit) { unit.world().set_track_deg(track_deg); });
}
KEEPALIVE void simulator_set_climb_mm_s(int climb_mm_s) {
    on_unit([&](auto& unit) { unit.world().set_climb_mm_s(climb_mm_s); });
}
KEEPALIVE void simulator_set_turn_dps_e1(int turn_dps_e1) {
    on_unit([&](auto& unit) { unit.world().set_turn_dps(turn_dps_e1 / 10.0); });
}
KEEPALIVE void simulator_set_slip_mg(int slip_mg) { g_simulator.world().set_slip_mg(slip_mg); }
// The air outside, in pascals.
KEEPALIVE void simulator_set_airmass_qnh_pa(int qnh_pa) {
    on_unit([&](auto& unit) { unit.world().set_airmass_qnh_pa(static_cast<uint32_t>(qnh_pa)); });
}
// The cell and the cable: the same two facts the board can read on silicon.
KEEPALIVE void simulator_set_battery_mv(int battery_mv) {
    on_unit([&](auto& unit) { unit.world().set_battery_mv(battery_mv); });
}
KEEPALIVE void simulator_set_external_power(int on) {
    on_unit([&](auto& unit) { unit.world().set_external_power(on != 0); });
}
KEEPALIVE void simulator_set_position(int latitude_1e7, int longitude_1e7) {
    on_unit([&](auto& unit) {
        unit.world().gnss().latitude_1e7 = latitude_1e7;
        unit.world().gnss().longitude_1e7 = longitude_1e7;
    });
}

// alptas picks what the aircraft is equipped with: 0 is ADS-L, anything else is
// ALP-TAS. Both land on the same two M-band channels, so it is the shared sync
// window that decides whether we hear it, not the button.
static protocol::System system_of(int alptas) {
    return alptas != 0 ? protocol::System::Alptas : protocol::System::AdslDirect;
}

KEEPALIVE int simulator_add_aircraft(int north_m, int east_m, int up_m, int speed_mps,
                                     int track_deg, int turn_dps_e1, int climb_mm_s, int alptas,
                                     int address) {
    return g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg, -1, -1,
                                            system_of(alptas), turn_dps_e1 / 10.0,
                                            climb_mm_s / 1000.0, static_cast<uint32_t>(address));
}
// phase_ms/slot below zero let the aircraft pick its own instant, as a
// conforming transmitter does. Pinned, they put a burst where the dwell map
// says we should or should not hear it.
KEEPALIVE void simulator_add_aircraft_at(int north_m, int east_m, int up_m, int speed_mps,
                                         int track_deg, int phase_ms, int slot, int alptas) {
    g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg, phase_ms, slot,
                                     system_of(alptas));
}
KEEPALIVE void simulator_add_threat(int alptas) {
    g_simulator.world().add_threat(system_of(alptas));
}
KEEPALIVE int simulator_tx_named() {
    return static_cast<int>(g_simulator.product().state().air.tx_named);
}
KEEPALIVE int simulator_rx_named() {
    return static_cast<int>(g_simulator.product().state().air.rx_named);
}
KEEPALIVE int simulator_prompt() {
    return g_simulator.product().screen().prompt() != comms::Pending::None ? 1 : 0;
}
KEEPALIVE int simulator_send_config(const char* json) {
    return g_simulator.world().send_config(json) ? 1 : 0;
}
KEEPALIVE void simulator_name_aircraft(int index, const char* callsign) {
    on_unit([&](auto& unit) { unit.world().name_aircraft(index, callsign); });
}
KEEPALIVE void simulator_clear_aircraft() {
    on_unit([](auto& unit) { unit.world().clear_aircraft(); });
}
KEEPALIVE void simulator_remove_aircraft(int index) {
    on_unit([&](auto& unit) { unit.world().remove_aircraft(index); });
}
KEEPALIVE int simulator_formation_members() {
    return on_unit([](auto& unit) { return unit.product().alarm().formation_members(); });
}
KEEPALIVE int simulator_aircraft_count() {
    return on_unit([](auto& unit) { return unit.world().aircraft_count(); });
}

// Layout: byte = panel[y*stride + (x>>3)], black if (byte & (0x80 >> (x&7))).
KEEPALIVE const unsigned char* simulator_panel() { return g_simulator.panel().data(); }
KEEPALIVE int simulator_panel_width() { return g_simulator.panel().width(); }
KEEPALIVE int simulator_panel_height() { return g_simulator.panel().height(); }
KEEPALIVE int simulator_panel_stride() { return go::Glass::kStride; }

KEEPALIVE int simulator_panel_refreshing() { return g_simulator.panel_refreshing() ? 1 : 0; }
KEEPALIVE int simulator_panel_refresh_is_full() {
    return g_simulator.panel_refresh_is_full() ? 1 : 0;
}
KEEPALIVE int simulator_present_count() { return g_simulator.present_count(); }

KEEPALIVE int simulator_backlight_on() { return g_simulator.backlight() ? 1 : 0; }
KEEPALIVE int simulator_panel_powered() { return g_simulator.panel_powered() ? 1 : 0; }
KEEPALIVE int simulator_page() { return static_cast<int>(g_simulator.product().screen().page()); }
KEEPALIVE int simulator_menu_open() {
    return g_simulator.product().screen().mode() == go::Mode::Menu ? 1 : 0;
}
KEEPALIVE int simulator_capabilities() {
    return static_cast<int>(on_unit([](auto& unit) { return unit.product().capabilities(); }));
}
KEEPALIVE int simulator_fix_valid() { return flying_state().own.fix_valid ? 1 : 0; }
KEEPALIVE int simulator_satellites() { return flying_state().own.satellites; }
KEEPALIVE int simulator_latitude_1e7() { return flying_state().own.latitude_1e7; }
KEEPALIVE int simulator_longitude_1e7() { return flying_state().own.longitude_1e7; }
KEEPALIVE int simulator_altitude_hae_mm() { return flying_state().own.altitude_hae_mm; }
KEEPALIVE int simulator_pressure_mpa() {
    return static_cast<int>(flying_state().barometer.pressure_mpa);
}
KEEPALIVE int simulator_battery_mv() { return flying_state().power.battery.cell_mv; }
KEEPALIVE int simulator_battery_percent() { return flying_state().power.battery.percent; }
KEEPALIVE int simulator_battery_charging() { return flying_state().power.battery.charging ? 1 : 0; }
KEEPALIVE int simulator_speed_mm_s() { return flying_state().own.speed_mm_s; }
KEEPALIVE int simulator_track_cdeg() { return flying_state().own.track_cdeg; }
KEEPALIVE int simulator_climb_mm_s() { return flying_state().own.climb_mm_s; }
KEEPALIVE int simulator_turn_cdps() { return flying_state().own.turn_cdps; }
KEEPALIVE int simulator_traffic_count() { return flying_state().traffic.count(); }
KEEPALIVE int simulator_alarm_level() { return traffic::to_number(flying_state().alarm_level); }
KEEPALIVE int simulator_alarm_dismissed() {
    const bus::State& state = flying_state();
    return state.alarm_level != traffic::Level::None &&
           state.alarm_undismissed == traffic::Level::None;
}
KEEPALIVE const char* simulator_lamp() {
    return indication::to_string(on_unit([](auto& unit) { return unit.lamp_since_last_look(); }));
}
KEEPALIVE int simulator_shutdown_phase() {
    return static_cast<int>(on_unit([](auto& unit) { return unit.product().shutdown().phase(); }));
}
KEEPALIVE int simulator_rx_ok() {
    return static_cast<int>(g_simulator.product().state().air.rx_ok);
}
KEEPALIVE int simulator_rx_bad() {
    return static_cast<int>(g_simulator.product().state().air.rx_bad);
}
KEEPALIVE int simulator_rx_wait() {
    return static_cast<int>(g_simulator.product().state().air.rx_wait);
}
KEEPALIVE int simulator_rx_type() {
    return static_cast<int>(g_simulator.product().state().air.rx_type);
}
KEEPALIVE int simulator_rx_unframed() {
    return static_cast<int>(g_simulator.product().state().air.rx_unframed);
}
KEEPALIVE int simulator_rx_miskeyed() {
    return static_cast<int>(g_simulator.product().state().air.rx_miskeyed);
}
KEEPALIVE int simulator_tx_ok() {
    return static_cast<int>(g_simulator.product().state().air.tx_ok);
}
KEEPALIVE int simulator_tx_lost() {
    return static_cast<int>(g_simulator.product().state().air.tx_lost);
}
KEEPALIVE int simulator_slot_state() {
    return static_cast<int>(g_simulator.product().state().rf.plan.state);
}
KEEPALIVE int simulator_dwell_frequency_khz() {
    return static_cast<int>(g_simulator.product().state().rf.plan.frequency_hz / 1000);
}

// The tape: every burst that was on the air, heard or not.
KEEPALIVE int simulator_air_record_count() { return g_simulator.world().air().record_count(); }
KEEPALIVE int simulator_air_phase_ms(int index) {
    return g_simulator.world().air().record_at(index).phase_ms;
}
KEEPALIVE int simulator_air_event(int index) {
    return static_cast<int>(g_simulator.world().air().record_at(index).event);
}
KEEPALIVE int simulator_air_frequency_khz(int index) {
    return static_cast<int>(g_simulator.world().air().record_at(index).frequency_hz / 1000);
}
KEEPALIVE int simulator_air_rssi_dbm(int index) {
    return g_simulator.world().air().record_at(index).rssi_dbm;
}
KEEPALIVE const char* simulator_air_line(int index) {
    static char line[160];
    g_simulator.world().air().format(index, line, sizeof(line));
    return line;
}
KEEPALIVE int simulator_air_deaf() { return static_cast<int>(g_simulator.world().air().deaf()); }
KEEPALIVE int simulator_air_collisions() {
    return static_cast<int>(g_simulator.world().air().collisions());
}
// INFO: fc 13nov26 what Bravo sent a phone, oldest first, so a page can show the EFB's stream
KEEPALIVE int simulator_nmea_count() { return static_cast<int>(g_bravo.sentences().size()); }
KEEPALIVE const char* simulator_nmea_line(int index) {
    const auto& sentences = g_bravo.sentences();
    if (index < 0 || index >= static_cast<int>(sentences.size())) return "";
    return sentences[static_cast<size_t>(index)].c_str();
}
KEEPALIVE int simulator_adsb_heard() { return static_cast<int>(g_bravo.world().adsb_heard()); }
KEEPALIVE int simulator_adsb_aircraft() { return g_bravo.product().adsb().assembly().count(); }
KEEPALIVE int simulator_failures() {
    return on_unit([](auto& unit) { return unit.world().failures(); });
}
}

int main() { return 0; }
