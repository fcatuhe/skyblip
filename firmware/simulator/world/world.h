#ifndef SKYBLIP_SIMULATOR_WORLD_WORLD_H
#define SKYBLIP_SIMULATOR_WORLD_WORLD_H

#include <cstdio>

#include "core/bus/state.h"
#include "core/events/link.h"
#include "core/flight/atmosphere.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/callsign.h"
#include "core/timing/slot.h"
#include "hardware/parts/gnss_truth.h"
#include "hardware/platform/host/meshtracker_x1.h"
#include "hardware/platform/host/platform.h"
#include "protocols/common/geometry.h"
#include "simulator/world/adsb_out.h"
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
    // INFO: fc 13nov26 an aircraft with ADS-B Out only has system Unknown, silent on 868
    AdsbOut adsb{};
    uint32_t address{0};
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
    char callsign[protocol::kInformationTextBytes + 1]{0};
};

struct AdsbDatum {
    int32_t own_hae_mm{0};
    int32_t geoid_separation_mm{0};
    // INFO: fc 13nov26 what the airmass adds to a height above sea level
    int32_t pressure_offset_mm{0};
};

class Sky {
   public:
    static constexpr int kMaxAircraft = 32;

    explicit Sky(const models::GnssTruth& own) : own_(own) {}

    int add_aircraft(double north_m, double east_m, double up_m, double speed_mps = 30,
                     double track_deg = 270, int phase_ms = -1, int slot = -1,
                     protocol::System system = protocol::System::AdslDirect, double turn_dps = 0,
                     double climb_mps = 0, uint32_t address = 0, const AdsbOut& adsb = AdsbOut{});
    // INFO: fc 14sep26 a kilometre ahead on our track, reciprocal, sinking through our level
    int add_threat(protocol::System system = protocol::System::AdslDirect,
                   const AdsbOut& adsb = AdsbOut{}) {
        return add_aircraft(0, 1000, 30, 40, 270, -1, -1, system, 0, kThreatSinkMps, 0, adsb);
    }
    void clear_aircraft();
    void remove_aircraft(int index);
    void name_aircraft(int index, const char* callsign);
    int aircraft_count() const;
    // INFO: fc 03aug26 where the world says an aircraft is, not the last report the firmware holds
    const VirtualAircraft* aircraft_at(int index) const;
    double separation_m(int index) const;

    Air& air() { return air_; }
    uint32_t adsb_sent() const { return adsb_sent_; }
    uint32_t adsb_heard() const { return adsb_heard_; }

   protected:
    void restart() {
        origin_valid_ = false;
        squittering_ = false;
        adsb_sent_ = 0;
        adsb_heard_ = 0;
    }
    void set_origin();
    void move_aircraft(uint32_t now_ms);
    void fly_868(uint64_t now_us, const model::Ownship& own, models::Sx1262& radio);
    void fly_1090(uint64_t now_us, const AdsbDatum& datum, models::Lr2021& radio);

   private:
    double own_north_m() const;
    double own_east_m() const;
    int32_t latitude_1e7_of(const VirtualAircraft& aircraft) const;
    int32_t longitude_1e7_of(const VirtualAircraft& aircraft) const;
    void schedule_second(uint64_t epoch_us, const model::Ownship& own);
    void transmit(VirtualAircraft& aircraft, uint64_t epoch_us, const model::Ownship& own);
    void name_itself(const VirtualAircraft& aircraft, uint64_t epoch_us, uint32_t utc_s,
                     double range_m);
    void relay(uint64_t epoch_us, const model::Ownship& own);
    model::AircraftObservation as_relayed(const VirtualAircraft& aircraft,
                                          const model::Ownship& own) const;
    void squitter(const VirtualAircraft& aircraft, uint64_t from_us, uint64_t to_us,
                  const AdsbDatum& datum, models::Lr2021& radio);
    void send(const AdsbFrame& frame, models::Lr2021& radio);
    static int8_t rssi_at(double range_m);

    static constexpr double kThreatSinkMps = -3;
    // INFO: fc 03aug26 a skyPost is a mast 12 km away at 500 mW e.r.p. (C.4): background, but heard
    static constexpr double kGroundStationRangeM = 12000;

    const models::GnssTruth& own_;
    Air air_{};
    protocol::AdslUplink uplink_{};
    VirtualAircraft aircraft_[kMaxAircraft]{};
    uint32_t aircraft_moved_ms_{0};
    int32_t origin_latitude_1e7_{0};
    int32_t origin_longitude_1e7_{0};
    bool origin_valid_{false};
    uint32_t scheduled_second_{0};
    bool scheduled_{false};
    uint64_t squittered_until_us_{0};
    bool squittering_{false};
    uint32_t adsb_sent_{0};
    uint32_t adsb_heard_{0};
};

static_assert(Air::kMaxBursts >= 2 * Sky::kMaxAircraft + 1,
              "a second's direct bursts, their callsigns and the relay must all fit the air");

// The sky, the air and the ground the firmware flies through. It drives the part
// models only: virtual aircraft are broadcast as genuine scrambled ADS-L frames
// and own-ship motion as genuine NMEA, so the production receive and parse paths
// are what run.
template <class Platform>
class World : public Sky {
   public:
    static constexpr int16_t kLevelFlightUpMg = 1000;

    explicit World(Platform& platform) : Sky(gnss_of(platform)), platform_(platform) {}

    void step(uint32_t now_ms, const bus::State& state);
    void update_inertial();

    void load(const Scenario& scenario);

    auto& gnss() { return gnss_of(platform_); }
    const auto& gnss() const { return gnss_of(platform_); }
    auto& barometer() { return barometer_of(platform_); }
    Platform& platform() { return platform_; }

    void set_fix(bool on) { gnss().fix_valid = on; }
    void set_satellites(int satellites) {
        gnss().satellites =
            static_cast<uint8_t>(satellites < 0 ? 0 : (satellites > 32 ? 32 : satellites));
    }
    void set_altitude_m(int32_t altitude_m) { gnss().altitude_m = altitude_m; }
    void set_speed_kt(int32_t speed_kt) {
        gnss().speed_mm_s = wire::divide_rounded(speed_kt * 514444, 1000);
    }
    void set_track_deg(int32_t track_deg) { gnss().track_deg = ((track_deg % 360) + 360) % 360; }
    void set_turn_dps(double turn_dps) { gnss().turn_dps = turn_dps; }
    void set_climb_mm_s(int32_t climb_mm_s) { gnss().climb_mm_s = climb_mm_s; }
    // The weather, not a setting: the sea-level pressure of the air the aircraft
    // is flying through. The barometer reads what that implies at its altitude.
    void set_airmass_qnh_pa(uint32_t qnh_pa) { airmass_qnh_pa_ = qnh_pa; }
    void set_slip_mg(int32_t slip_mg) { slip_mg_ = slip_mg; }
    void set_pps_locked(bool on) { platform_.pps().set_locked(on); }
    // The cell as the world holds it: what the divider reads, and whether a
    // cable is in. Everything else about the battery is the firmware's opinion.
    void set_battery_mv(int32_t battery_mv) {
        platform_.battery().divider_mv = static_cast<uint16_t>(battery_mv < 0 ? 0 : battery_mv);
    }
    void set_external_power(bool on) { platform_.battery().external_power = on; }
    void press_button() { press_pending_ = true; }
    void hold_button(bool down) { button_held_ = down; }
    void tap_pad() { tap_pending_ = true; }
    void hold_pad(bool down) { pad_held_ = down; }

    // The pilot's phone walking up and walking away. It drives the platform's own
    // comms::LinkSession, which is the object Zephyr's connection callbacks drive
    // on silicon, so the device learns about the central the same way either side.
    void connect_companion(uint16_t session_id = 1) { platform_.link().raise_link(session_id); }
    // What a phone says on the config endpoint, arriving where the board polls
    // it: the only path a callsign reaches this device by.
    bool send_config(const char* json);
    void disconnect_companion() { platform_.link().drop_link(); }

    // One altitude, two sensors: the GNSS model integrates climb into its own
    // altitude and the barometer reads THAT, exactly as both would see the same
    // air. Setting them apart would show a climb rate no real pair can produce.
    // The airmass shifts the pressure the sensor sees, as weather does, without
    // moving the aircraft.
    template <class Barometer>
    void feel_air(Barometer& barometer) const {
        const int32_t altitude_msl_mm = gnss().altitude_mm() - gnss().geoid_separation_m * 1000;
        barometer.set_pressure_mpa(flight::altitude_mm_to_pressure_mpa(
            altitude_msl_mm + flight::pressure_to_altitude_mm(airmass_qnh_pa_ * 1000)));
        barometer.set_temperature_dc(flight::isa_temperature_dc(altitude_msl_mm));
    }
    // INFO: fc 14nov26 a Bravo aboard hears this sky's 1090 MHz, and a sky has one such receiver
    void squitter_to(models::Lr2021& radio) {
        fly_1090(platform_.clock().microseconds(), datum(), radio);
    }

    int failures() const { return failures_; }
    const char* first_failure() const { return failure_[0] == 0 ? nullptr : failure_; }
    bool finished(uint32_t now_ms) const {
        return scenario_.duration_ms != 0 && now_ms - start_ms_ >= scenario_.duration_ms;
    }

   private:
    static models::L76k& gnss_of(platform::host::Platform& go) { return go.chips().gnss; }
    static models::Ag3335& gnss_of(platform::host::MeshtrackerX1& bravo) { return bravo.gnss(); }
    static models::Bme280& barometer_of(platform::host::Platform& go) {
        return go.barometer().chip;
    }
    static models::Spa06& barometer_of(platform::host::MeshtrackerX1& bravo) {
        return bravo.barometer();
    }
    static void fit_barometer(platform::host::Platform& go, bool on) { go.barometer().fitted = on; }
    static void fit_barometer(platform::host::MeshtrackerX1& bravo, bool on) {
        bravo.barometer().answers = on;
    }
    static void set_button_down(platform::host::Platform& go, bool down) {
        go.board_gpio().button_down = down;
    }
    static void set_button_down(platform::host::MeshtrackerX1& bravo, bool down) {
        bravo.set_button_down(down);
    }
    static void set_pad_down(platform::host::Platform& go, bool down) {
        go.board_gpio().pad_down = down;
    }
    // INFO: fc 13nov26 an X1 has no touch pad and no inertial unit
    static void set_pad_down(platform::host::MeshtrackerX1&, bool) {}
    static void lean(platform::host::Platform& go, int16_t up_mg, int16_t lateral_mg) {
        go.chips().imu.set_acceleration(up_mg, lateral_mg, 0);
    }
    static void lean(platform::host::MeshtrackerX1&, int16_t, int16_t) {}
    void listen(uint64_t now_us, const bus::State& state, platform::host::Platform& go) {
        fly_868(now_us, state.own, go.chips().radio);
    }
    void listen(uint64_t now_us, const bus::State&, platform::host::MeshtrackerX1& bravo) {
        fly_1090(now_us, datum(), bravo.radio());
    }

    AdsbDatum datum() const {
        AdsbDatum datum;
        datum.own_hae_mm = gnss().altitude_mm();
        datum.geoid_separation_mm = gnss().geoid_separation_m * 1000;
        datum.pressure_offset_mm = flight::pressure_to_altitude_mm(airmass_qnh_pa_ * 1000);
        return datum;
    }
    void service_button(uint32_t now_ms);
    void service_pad(uint32_t now_ms);
    void apply_events(uint32_t now_ms, const bus::State& state);
    void fail(const char* what) {
        failures_++;
        if (failure_[0] == 0) std::snprintf(failure_, sizeof(failure_), "%s", what);
    }

    // A modelled press has to last longer than the board's debounce window or
    // the firmware is right to ignore it.
    static constexpr uint32_t kPressMs = 60;

    Platform& platform_;
    Scenario scenario_{};
    size_t next_event_{0};
    uint32_t start_ms_{0};
    uint32_t airmass_qnh_pa_{flight::kIsaSeaLevelPa};
    int32_t slip_mg_{0};
    uint32_t press_since_ms_{0};
    uint32_t tap_since_ms_{0};
    int failures_{0};
    bool started_{false};
    bool press_pending_{false};
    bool pressing_{false};
    bool button_held_{false};
    bool tap_pending_{false};
    bool tapping_{false};
    bool pad_held_{false};
    char failure_[96]{0};
};

}  // namespace skyblip::simulator

#endif
