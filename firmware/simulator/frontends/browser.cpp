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
}

extern "C" {

// INFO: fc 25sep26 a setup that failed draws the self-test page, which is the answer the page shows
KEEPALIVE void simulator_setup() { (void)g_simulator.setup(); }
KEEPALIVE void simulator_step(unsigned ms) { g_simulator.step(ms); }
KEEPALIVE int simulator_park_refusal(unsigned ms) { return g_simulator.park_refusal(ms) ? 1 : 0; }
KEEPALIVE int simulator_load_scenario(const char* json, int len) {
    simulator::Scenario s;
    if (!simulator::parse_scenario(json, len, s)) return 0;
    g_simulator.load(s);
    return 1;
}
KEEPALIVE int simulator_mode() { return static_cast<int>(g_simulator.mode()); }

KEEPALIVE void simulator_button() { g_simulator.world().press_button(); }
KEEPALIVE void simulator_button_down(int down) { g_simulator.world().hold_button(down != 0); }
KEEPALIVE void simulator_pad_down(int down) { g_simulator.world().hold_pad(down != 0); }
KEEPALIVE void simulator_backlight(int on) {
    g_simulator.product().screen().set_backlight(on != 0);
}
KEEPALIVE void simulator_power(int on) { g_simulator.product().screen().set_power(on != 0); }
KEEPALIVE void simulator_set_range(int step) {
    g_simulator.product().screen().set_range_step(step);
}

KEEPALIVE void simulator_set_fix(int on) { g_simulator.world().set_fix(on != 0); }
KEEPALIVE void simulator_set_pps(int on) { g_simulator.world().set_pps_locked(on != 0); }
KEEPALIVE void simulator_set_sats(int n) { g_simulator.world().set_sats(n); }
KEEPALIVE void simulator_set_alt(int m) { g_simulator.world().set_altitude_m(m); }
KEEPALIVE void simulator_set_speed(int kt) { g_simulator.world().set_speed_kt(kt); }
KEEPALIVE void simulator_set_track(int deg) { g_simulator.world().set_track_deg(deg); }
KEEPALIVE void simulator_set_climb(int mm_s) { g_simulator.world().set_climb_mm_s(mm_s); }
KEEPALIVE void simulator_set_turn(int dps_e1) { g_simulator.world().set_turn_dps(dps_e1 / 10.0); }
KEEPALIVE void simulator_set_slip(int mg) { g_simulator.world().set_slip_mg(mg); }
// The air outside, in pascals.
KEEPALIVE void simulator_set_airmass(int pa) {
    g_simulator.world().set_airmass_qnh_pa(static_cast<uint32_t>(pa));
}
// The cell and the cable: the same two facts the board can read on silicon.
KEEPALIVE void simulator_set_battery_mv(int mv) { g_simulator.world().set_battery_mv(mv); }
KEEPALIVE void simulator_set_external_power(int on) {
    g_simulator.world().set_external_power(on != 0);
}
KEEPALIVE void simulator_set_position(int lat_1e7, int lon_1e7) {
    g_simulator.world().gnss().lat_1e7 = lat_1e7;
    g_simulator.world().gnss().lon_1e7 = lon_1e7;
}

// alptas picks what the aircraft is equipped with: 0 is ADS-L, anything else is
// ALP-TAS. Both land on the same two M-band channels, so it is the shared sync
// window that decides whether we hear it, not the button.
static protocol::System system_of(int alptas) {
    return alptas != 0 ? protocol::System::Alptas : protocol::System::AdslDirect;
}

KEEPALIVE void simulator_add_aircraft(int north_m, int east_m, int up_m, int speed_mps,
                                      int track_deg, int turn_dps_e1, int climb_mm_s, int alptas,
                                      int addr) {
    g_simulator.world().add_aircraft(north_m, east_m, up_m, speed_mps, track_deg, -1, -1,
                                     system_of(alptas), turn_dps_e1 / 10.0, climb_mm_s / 1000.0,
                                     static_cast<uint32_t>(addr));
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
KEEPALIVE void simulator_send_config(const char* json) { g_simulator.world().send_config(json); }
KEEPALIVE void simulator_name_aircraft(int index, const char* callsign) {
    g_simulator.world().name_aircraft(index, callsign);
}
KEEPALIVE void simulator_clear_traffic() { g_simulator.world().clear_aircraft(); }
KEEPALIVE int simulator_formation_members() {
    return g_simulator.product().alarm().formation_members();
}
KEEPALIVE int simulator_aircraft_count() { return g_simulator.world().aircraft_count(); }

// Layout: byte = fb[y*stride + (x>>3)], black if (byte & (0x80 >> (x&7))).
KEEPALIVE const unsigned char* simulator_fb() { return g_simulator.panel().data(); }
KEEPALIVE int simulator_fb_w() { return g_simulator.panel().width(); }
KEEPALIVE int simulator_fb_h() { return g_simulator.panel().height(); }
KEEPALIVE int simulator_fb_stride() { return go::Glass::kStride; }

KEEPALIVE int simulator_panel_refreshing() { return g_simulator.panel_refreshing() ? 1 : 0; }
KEEPALIVE int simulator_panel_refresh_is_full() {
    return g_simulator.panel_refresh_is_full() ? 1 : 0;
}
KEEPALIVE int simulator_present_count() { return g_simulator.present_count(); }

KEEPALIVE int simulator_backlight_on() { return g_simulator.backlight() ? 1 : 0; }
KEEPALIVE int simulator_powered() { return g_simulator.panel_powered() ? 1 : 0; }
KEEPALIVE int simulator_page() { return static_cast<int>(g_simulator.product().screen().page()); }
KEEPALIVE int simulator_menu_open() {
    return g_simulator.product().screen().mode() == go::Mode::Menu ? 1 : 0;
}
KEEPALIVE int simulator_capabilities() {
    return static_cast<int>(g_simulator.product().capabilities());
}
KEEPALIVE int simulator_fix_valid() { return g_simulator.product().state().own.fix_valid ? 1 : 0; }
KEEPALIVE int simulator_sats() { return g_simulator.product().state().own.sats; }
KEEPALIVE int simulator_lat_1e7() { return g_simulator.product().state().own.lat_1e7; }
KEEPALIVE int simulator_lon_1e7() { return g_simulator.product().state().own.lon_1e7; }
KEEPALIVE int simulator_alt_mm() { return g_simulator.product().state().own.alt_mm; }
KEEPALIVE int simulator_pressure_mpa() {
    return static_cast<int>(g_simulator.product().state().baro.pressure_mpa);
}
KEEPALIVE int simulator_battery_mv() {
    return g_simulator.product().state().power.battery.millivolts;
}
KEEPALIVE int simulator_battery_percent() {
    return g_simulator.product().state().power.battery.percent;
}
KEEPALIVE int simulator_battery_charging() {
    return g_simulator.product().state().power.battery.charging ? 1 : 0;
}
KEEPALIVE int simulator_speed_mm_s() { return g_simulator.product().state().own.speed_mm_s; }
KEEPALIVE int simulator_track_cdeg() { return g_simulator.product().state().own.track_cdeg; }
KEEPALIVE int simulator_climb_mm_s() { return g_simulator.product().state().own.climb_mm_s; }
KEEPALIVE int simulator_turn_cdps() { return g_simulator.product().state().own.turn_cdps; }
KEEPALIVE int simulator_traffic_count() { return g_simulator.product().state().traffic.count(); }
KEEPALIVE int simulator_alarm_level() {
    return traffic::to_number(g_simulator.product().state().alarm_level);
}
KEEPALIVE int simulator_alarm_dismissed() {
    const bus::State& state = g_simulator.product().state();
    return state.alarm_level != traffic::Level::None && state.alarm_live == traffic::Level::None;
}
KEEPALIVE int simulator_shutdown_phase() {
    return static_cast<int>(g_simulator.product().shutdown().phase());
}
KEEPALIVE int simulator_haptic_ms() { return g_simulator.haptic_ms(); }
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
KEEPALIVE int simulator_dwell_freq() {
    return static_cast<int>(g_simulator.product().state().rf.plan.freq_hz / 1000);
}

// The tape: every burst that was on the air, heard or not.
KEEPALIVE int simulator_air_count() { return g_simulator.world().air().record_count(); }
KEEPALIVE int simulator_air_phase_ms(int i) { return g_simulator.world().air().record(i).phase_ms; }
KEEPALIVE int simulator_air_event(int i) {
    return static_cast<int>(g_simulator.world().air().record(i).event);
}
KEEPALIVE int simulator_air_freq_khz(int i) {
    return static_cast<int>(g_simulator.world().air().record(i).freq_hz / 1000);
}
KEEPALIVE int simulator_air_rssi(int i) { return g_simulator.world().air().record(i).rssi_dbm; }
KEEPALIVE const char* simulator_air_line(int i) {
    static char line[160];
    g_simulator.world().air().format(i, line, sizeof(line));
    return line;
}
KEEPALIVE int simulator_air_deaf() { return static_cast<int>(g_simulator.world().air().deaf()); }
KEEPALIVE int simulator_air_collisions() {
    return static_cast<int>(g_simulator.world().air().collisions());
}
KEEPALIVE int simulator_failures() { return g_simulator.world().failures(); }
}

int main() { return 0; }
