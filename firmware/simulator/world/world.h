#ifndef SKYBLIP_SIMULATOR_WORLD_WORLD_H
#define SKYBLIP_SIMULATOR_WORLD_WORLD_H

#include "core/bus/state.h"
#include "core/events/link.h"
#include "core/flight/atmosphere.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/timing/slot.h"
#include "hardware/platform/host/platform.h"
#include "simulator/world/air.h"
#include "simulator/world/scenario.h"

namespace skyblip::simulator {

struct VirtualAircraft {
    bool used{false};
    // Which system this aircraft is equipped with. AdslDirect and Alptas are
    // Manchester bursts on the same two M-band channels, which is exactly why
    // one dwell can hear them. AdslUplink is not a fit at all: it is an aircraft
    // this device never hears for itself, reaching it only because a skyPost on
    // the ground heard it and relays it on the O band.
    protocol::System system{protocol::System::AdslDirect};
    uint32_t addr{0};
    // North and east of the world's origin, not of own-ship: two aircraft that
    // both manoeuvre only describe one encounter if they fly over the same
    // ground. up_m stays own-relative, as the air in one thermal is.
    double north_m{0}, east_m{0}, up_m{0};
    double speed_mps{30};
    double track_deg{270};
    // Degrees per second, positive to the right: a target holding a steady turn,
    // which is what a glider in a thermal is doing.
    double turn_dps{0};
    double climb_mps{0};
    // Where in the second this aircraft transmits, and on which M-band channel.
    // Below zero it picks its own instant per second the way a conforming
    // transmitter does. Pinned, it is the knob that proves our dwell map.
    int phase_ms{-1};
    int slot{-1};
    uint32_t transmissions{0};
    // Empty for an aircraft nobody named, which is most of them: only a skyBlip
    // or an OGN tracker puts its registration on air.
    char callsign[protocol::AdslPacket::kInfoMsgBytes + 1]{0};
};

// The sky, the air and the ground the firmware flies through. It drives the part
// models only: virtual aircraft are broadcast as genuine scrambled ADS-L frames
// and own-ship motion as genuine NMEA, so the production receive and parse paths
// are what run.
class World {
   public:
    static constexpr int kMaxAircraft = 8;
    static constexpr int16_t kLevelFlightUpMg = 1000;

    explicit World(platform::host::Platform& platform) : platform_(platform) {}

    void step(uint32_t now_ms, const bus::State& state);
    void update_inertial();

    void load(const Scenario& scenario);

    int add_aircraft(double north_m, double east_m, double up_m, double speed_mps = 30,
                     double track_deg = 270, int phase_ms = -1, int slot = -1,
                     protocol::System system = protocol::System::AdslDirect, double turn_dps = 0,
                     double climb_mps = 0, uint32_t addr = 0);
    // INFO: fc 14sep26 a kilometre ahead on our track, reciprocal, sinking through our level
    int add_threat(protocol::System system = protocol::System::AdslDirect) {
        return add_aircraft(0, 1000, 30, 40, 270, -1, -1, system, 0, kThreatSinkMps);
    }
    void clear_aircraft();
    void remove_aircraft(int index);
    void name_aircraft(int index, const char* callsign);
    int aircraft_count() const;
    // Where the world says the two aircraft actually are, which is not what the
    // firmware knows: the device only ever has the target's last report.
    const VirtualAircraft* aircraft_at(int index) const;
    double separation_m(int index) const;

    Air& air() { return air_; }
    models::L76k& gnss() { return platform_.chips().gnss; }
    const models::L76k& gnss() const { return platform_.chips().gnss; }
    models::Bme280& baro() { return platform_.baro().chip; }
    models::Bhi260& imu() { return platform_.chips().imu; }
    platform::host::Platform& platform() { return platform_; }

    void set_fix(bool on) { gnss().fix = on; }
    void set_sats(int n) { gnss().sats = static_cast<uint8_t>(n < 0 ? 0 : (n > 32 ? 32 : n)); }
    void set_altitude_m(int32_t m) { gnss().alt_m = m; }
    void set_speed_kt(int32_t kt) { gnss().speed_mm_s = div_round(kt * 514444, 1000); }
    void set_track_deg(int32_t deg) { gnss().track_deg = ((deg % 360) + 360) % 360; }
    void set_turn_dps(double dps) { gnss().turn_dps = dps; }
    void set_climb_mm_s(int32_t mm_s) { gnss().climb_mm_s = mm_s; }
    // The weather, not a setting: the sea-level pressure of the air the aircraft
    // is flying through. The barometer reads what that implies at its altitude.
    void set_airmass_qnh_pa(uint32_t pa) { airmass_qnh_pa_ = pa; }
    void set_slip_mg(int32_t mg) { slip_mg_ = mg; }
    void set_pps_locked(bool on) { platform_.pps().set_locked(on); }
    // The cell as the world holds it: what the divider reads, and whether a
    // cable is in. Everything else about the battery is the firmware's opinion.
    void set_battery_mv(int32_t mv) {
        platform_.battery().millivolts = static_cast<uint16_t>(mv < 0 ? 0 : mv);
    }
    void set_external_power(bool on) { platform_.battery().external_power = on; }
    void press_button() { press_pending_ = true; }
    void hold_button(bool down) { holding_ = down; }
    void tap_pad() { tap_pending_ = true; }
    void hold_pad(bool down) { pad_held_ = down; }

    // The pilot's phone walking up and walking away. It drives the platform's own
    // comms::LinkSession, which is the object Zephyr's connection callbacks drive
    // on silicon, so the device learns about the central the same way either side.
    void connect_companion(uint16_t session_id = 1) { platform_.link().raise_link(session_id); }
    // What a phone says on the config endpoint, arriving where the board polls
    // it: the only path a callsign reaches this device by.
    void send_config(const char* json);
    void disconnect_companion() { platform_.link().drop_link(); }

    int failures() const { return failures_; }
    const char* first_failure() const { return failure_[0] == 0 ? nullptr : failure_; }
    bool finished(uint32_t now_ms) const {
        return scenario_.duration_ms != 0 && now_ms - start_ms_ >= scenario_.duration_ms;
    }

   private:
    void service_button(uint32_t now_ms);
    void service_pad(uint32_t now_ms);
    void set_origin();
    double own_north_m() const;
    double own_east_m() const;
    void service_aircraft(uint32_t now_ms, const model::OwnState& own);
    void schedule_second(uint64_t epoch_us, const model::OwnState& own);
    void transmit(VirtualAircraft& aircraft, uint64_t epoch_us, const model::OwnState& own);
    void name_itself(const VirtualAircraft& aircraft, uint64_t epoch_us, uint32_t utc,
                     double range_m);
    void relay(uint64_t epoch_us, const model::OwnState& own);
    model::AircraftObs as_relayed(const VirtualAircraft& aircraft,
                                  const model::OwnState& own) const;
    static int8_t rssi_at(double range_m);
    void apply_events(uint32_t now_ms, const bus::State& state);
    void fail(const char* what);

    // A modelled press has to last longer than the board's debounce window or
    // the firmware is right to ignore it.
    static constexpr uint32_t kPressMs = 60;
    static constexpr double kThreatSinkMps = -3;

    // Where the ground station is, so a relayed burst arrives at a level a
    // receiver can plausibly hear. A skyPost is a fixed site with a mast and
    // 500 mW e.r.p. (§C.4), not another glider, so it is placed far enough away
    // to be background and loud enough to be heard.
    static constexpr double kGroundStationRangeM = 12000;

    platform::host::Platform& platform_;
    Air air_{};
    protocol::AdslUplink uplink_{};
    VirtualAircraft aircraft_[kMaxAircraft]{};
    Scenario scenario_{};
    size_t next_event_{0};
    uint32_t start_ms_{0};
    uint32_t last_aircraft_ms_{0};
    uint32_t airmass_qnh_pa_{flight::kIsaSeaLevelPa};
    int32_t slip_mg_{0};
    uint32_t press_since_ms_{0};
    uint32_t tap_since_ms_{0};
    int32_t origin_lat_1e7_{0};
    int32_t origin_lon_1e7_{0};
    bool origin_set_{false};
    uint32_t scheduled_sec_{0};
    bool scheduled_{false};
    int failures_{0};
    bool armed_{false};
    bool press_pending_{false};
    bool pressing_{false};
    bool holding_{false};
    bool tap_pending_{false};
    bool tapping_{false};
    bool pad_held_{false};
    char failure_[96]{0};
};

}  // namespace skyblip::simulator

#endif
