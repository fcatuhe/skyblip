#include "simulator/world/world.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/timing/transmit.h"
#include "core/units/units.h"

namespace skyblip::simulator {

namespace {
constexpr double kPi = 3.14159265358979;
constexpr double kMetresPerDegreeLatitude = 111320.0;
}  // namespace

void World::step(uint32_t now_ms, const bus::State& state) {
    if (!started_) {
        started_ = true;
        start_ms_ = now_ms;
        set_origin();
    }
    gnss().tick(now_ms);

    // One altitude, two sensors: the GNSS model integrates climb into its own
    // altitude and the barometer reads THAT, exactly as both would see the same
    // air. Setting them apart would show a climb rate no real pair can produce.
    // The airmass shifts the pressure the sensor sees, as weather does, without
    // moving the aircraft.
    const int32_t altitude_msl_mm = gnss().altitude_mm() - gnss().geoid_separation_m * 1000;
    barometer().set_pressure_mpa(flight::altitude_mm_to_pressure_mpa(
        altitude_msl_mm + flight::pressure_to_altitude_mm(airmass_qnh_pa_ * 1000)));
    barometer().set_temperature_dc(flight::isa_temperature_dc(altitude_msl_mm));

    update_inertial();
    service_button(now_ms);
    service_pad(now_ms);
    service_aircraft(now_ms, state.own);
    apply_events(now_ms, state);

    const uint64_t now_us = platform_.clock().micros();
    const uint64_t epoch_us = now_us - now_us % 1000000;
    const uint32_t second = static_cast<uint32_t>(now_us / 1000000);
    if (!scheduled_ || second != scheduled_second_) {
        scheduled_ = true;
        scheduled_second_ = second;
        schedule_second(epoch_us, state.own);
    }
    air_.step(now_us, platform_.chips().radio);
}

void World::service_button(uint32_t now_ms) {
    if (press_pending_) {
        press_pending_ = false;
        press_since_ms_ = now_ms;
        pressing_ = true;
    }
    // A start instant and a flag, not an end instant: ports/clock.h's rule, and the
    // same shape as the wrap bug section M found in the transmitter. A simulator
    // that models a button with arithmetic nobody may copy is worse than useless.
    platform_.board_gpio().button_down =
        button_held_ || (pressing_ && now_ms - press_since_ms_ < kPressMs);
}

void World::service_pad(uint32_t now_ms) {
    if (tap_pending_) {
        tap_pending_ = false;
        tap_since_ms_ = now_ms;
        tapping_ = true;
    }
    platform_.board_gpio().pad_down = pad_held_ || (tapping_ && now_ms - tap_since_ms_ < kPressMs);
}

// INFO: fc 03aug26 An aircraft is placed where the pilot would point at it - so
// many metres north and east of own-ship, now - and from that instant it flies
// over the ground on its own rather than being towed along by own-ship's motion.
// Before this the two positions were only ever differenced, so a scenario in
// which own-ship manoeuvres described no encounter at all: the range closed at
// the target's speed alone while the alarm was told both speeds.
void World::name_aircraft(int index, const char* callsign) {
    if (index < 0 || index >= kMaxAircraft || !aircraft_[index].used || callsign == nullptr) return;
    char* out = aircraft_[index].callsign;
    int n = 0;
    while (n < protocol::AdslPacket::kInfoTextBytes && callsign[n] != 0) {
        out[n] = callsign[n];
        n++;
    }
    out[n] = 0;
}

int World::add_aircraft(double north_m, double east_m, double up_m, double speed_mps,
                        double track_deg, int phase_ms, int slot, protocol::System system,
                        double turn_dps, double climb_mps, uint32_t address) {
    set_origin();
    for (int i = 0; i < kMaxAircraft; i++) {
        if (aircraft_[i].used) continue;
        aircraft_[i] = VirtualAircraft{};
        aircraft_[i].used = true;
        aircraft_[i].system = system;
        aircraft_[i].address = address != 0 ? address : 0x300000u + static_cast<uint32_t>(i) + 1u;
        aircraft_[i].north_m = own_north_m() + north_m;
        aircraft_[i].east_m = own_east_m() + east_m;
        aircraft_[i].up_m = up_m;
        aircraft_[i].speed_mps = speed_mps;
        aircraft_[i].track_deg = track_deg;
        aircraft_[i].turn_dps = turn_dps;
        aircraft_[i].climb_mps = climb_mps;
        aircraft_[i].phase_ms = phase_ms;
        aircraft_[i].slot = slot;
        return i;
    }
    return -1;
}

void World::set_origin() {
    if (origin_valid_) return;
    origin_valid_ = true;
    origin_latitude_1e7_ = gnss().latitude_1e7;
    origin_longitude_1e7_ = gnss().longitude_1e7;
}

double World::own_north_m() const {
    return (gnss().latitude_1e7 - origin_latitude_1e7_) * kMetresPerDegreeLatitude / 1e7;
}

double World::own_east_m() const {
    const double cos_latitude = std::cos(gnss().latitude_1e7 / 1e7 * kPi / 180.0);
    return (gnss().longitude_1e7 - origin_longitude_1e7_) * kMetresPerDegreeLatitude *
           cos_latitude / 1e7;
}

const VirtualAircraft* World::aircraft_at(int index) const {
    if (index < 0 || index >= kMaxAircraft || !aircraft_[index].used) return nullptr;
    return &aircraft_[index];
}

double World::separation_m(int index) const {
    const VirtualAircraft* aircraft = aircraft_at(index);
    if (aircraft == nullptr) return -1;
    const double north_m = aircraft->north_m - own_north_m();
    const double east_m = aircraft->east_m - own_east_m();
    return std::sqrt(north_m * north_m + east_m * east_m);
}

void World::clear_aircraft() {
    for (auto& aircraft : aircraft_) aircraft.used = false;
}

void World::remove_aircraft(int index) {
    if (index < 0 || index >= kMaxAircraft) return;
    aircraft_[index].used = false;
}

int World::aircraft_count() const {
    int n = 0;
    for (const auto& aircraft : aircraft_)
        if (aircraft.used) n++;
    return n;
}

void World::service_aircraft(uint32_t now_ms, const model::Ownship& own) {
    (void)own;
    if (now_ms - aircraft_moved_ms_ < 100) return;
    const double dt = (now_ms - aircraft_moved_ms_) / 1000.0;
    aircraft_moved_ms_ = now_ms;
    const double own_climb_mps = gnss().climb_mm_s / 1000.0;

    for (auto& aircraft : aircraft_) {
        if (!aircraft.used) continue;
        const double rad = (aircraft.track_deg + aircraft.turn_dps * dt * 0.5) * kPi / 180.0;
        aircraft.north_m += aircraft.speed_mps * std::cos(rad) * dt;
        aircraft.east_m += aircraft.speed_mps * std::sin(rad) * dt;
        aircraft.up_m += (aircraft.climb_mps - own_climb_mps) * dt;
        if (aircraft.turn_dps == 0) continue;
        aircraft.track_deg += aircraft.turn_dps * dt;
        while (aircraft.track_deg >= 360.0) aircraft.track_deg -= 360.0;
        while (aircraft.track_deg < 0.0) aircraft.track_deg += 360.0;
    }
}

// The accelerometer feels the ball and the wing load, and nothing here reads a
// rate: the hub's gyroscope is never subscribed to (hardware/parts/bhi260).
void World::update_inertial() {
    imu().set_acceleration(kLevelFlightUpMg, static_cast<int16_t>(-slip_mg_), 0);
}

// Every aircraft transmits once per second, at its own instant of the direct
// slot and on the channel that slot carries (ADS-L 4 SRD-860 issue 2 §C.2.5,
// §C.5). Scheduling the whole second up front is what a real transmitter's
// clock does, and it keeps the emission instant independent of how coarsely the
// simulation is stepped.
void World::schedule_second(uint64_t epoch_us, const model::Ownship& own) {
    for (auto& aircraft : aircraft_) {
        if (!aircraft.used || aircraft.system == protocol::System::AdslUplink) continue;
        transmit(aircraft, epoch_us, own);
    }
    relay(epoch_us, own);
}

// What the ground station knows about one aircraft, which is less than the
// aircraft's own frame carries: an address, a place, a height, a speed. No
// climb rate and no track, because an uplink record has no room for them - which
// is the whole reason a direct reception outranks a relay of the same aircraft.
model::AircraftObservation World::as_relayed(const VirtualAircraft& aircraft,
                                             const model::Ownship& own) const {
    const double cos_latitude = std::cos(origin_latitude_1e7_ / 1e7 * kPi / 180.0);
    model::AircraftObservation observation{};
    observation.address = aircraft.address;
    observation.address_mapping_table = 6;
    observation.aircraft_category = 4;
    observation.flight_state = 2;
    observation.speed_q = static_cast<uint16_t>(aircraft.speed_mps * 4);
    observation.speed_valid = true;
    observation.latitude_1e7 =
        origin_latitude_1e7_ +
        static_cast<int32_t>(aircraft.north_m * 1e7 / kMetresPerDegreeLatitude);
    observation.longitude_1e7 = origin_longitude_1e7_;
    if (cos_latitude > 0.01)
        observation.longitude_1e7 +=
            static_cast<int32_t>(aircraft.east_m * 1e7 / (kMetresPerDegreeLatitude * cos_latitude));
    observation.altitude_m =
        to_metres(Millimetres(own.altitude_hae_mm)).v + static_cast<int32_t>(aircraft.up_m);
    observation.position_valid = true;
    return observation;
}

// One skyPost, one frame a second, carrying every aircraft it heard: §C.5's
// uplink slot is 200..450 ms and our own ground station is on air from 210,
// finishing well before the dwell closes at 395 (core/timing/slot.h). A relay
// costs the aircraft nothing - it never transmits on the O band - so this is
// scheduled apart from the direct transmitters rather than per aircraft.
void World::relay(uint64_t epoch_us, const model::Ownship& own) {
    model::AircraftObservation relayed[protocol::AdslUplink::kMaxTargets];
    int n = 0;
    for (auto& aircraft : aircraft_) {
        if (!aircraft.used || aircraft.system != protocol::System::AdslUplink) continue;
        if (n >= protocol::AdslUplink::kMaxTargets) break;
        relayed[n++] = as_relayed(aircraft, own);
        aircraft.transmissions++;
    }
    if (n == 0) return;

    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    if (uplink_.encode(relayed, n, /*key_index=*/0, frame) != Status::Ok) return;
    uint8_t burst[protocol::kUplinkBurstBytes] = {0};
    const size_t burst_length = protocol::encode_oband(frame, burst);

    air_.emit(epoch_us + static_cast<uint64_t>(timing::kGroundEmitStartMs) * 1000, timing::kObandHz,
              burst, static_cast<uint16_t>(burst_length), rssi_at(kGroundStationRangeM),
              protocol::kUplinkChipRateBps);
}

// Free-space-ish: a burst 100 m away lands at -40 dBm and every decade of
// distance costs 20 dB, which puts a 10 km target near the receiver's floor.
int8_t World::rssi_at(double range_m) {
    const double d = range_m < 10.0 ? 10.0 : range_m;
    const double dbm = -40.0 - 20.0 * std::log10(d / 100.0);
    if (dbm < -127.0) return -127;
    return static_cast<int8_t>(dbm);
}

namespace {

int16_t climb_e8_of(const VirtualAircraft& aircraft) {
    constexpr double kEighthsPerMps = 8;
    return static_cast<int16_t>(std::lround(aircraft.climb_mps * kEighthsPerMps));
}

size_t callsign_burst(const VirtualAircraft& aircraft, uint8_t* chips) {
    protocol::AdslPacket p;
    protocol::from_own_callsign(p, aircraft.address, 6, aircraft.callsign);
    p.scramble();
    p.set_crc();
    return protocol::encode_mband(protocol::kAdslSyncWord, p.Data, protocol::kAdslFrameBytes,
                                  chips);
}

size_t adsl_burst(const VirtualAircraft& aircraft, uint32_t utc_s, int32_t altitude_m,
                  int32_t latitude_1e7, int32_t longitude_1e7, uint8_t* chips) {
    protocol::AdslPacket p;
    p.init();
    p.set_address(aircraft.address);
    p.set_address_mapping_table(6);
    p.TimeStamp = static_cast<uint8_t>((utc_s % 15) * 4);
    p.FlightState = 2;
    p.AircraftCategory = 4;
    p.Emergency = 1;
    p.set_latitude_1e7(latitude_1e7);
    p.set_longitude_1e7(longitude_1e7);
    p.set_altitude_m(altitude_m);
    p.set_speed_q(static_cast<uint16_t>(aircraft.speed_mps * 4));
    p.set_climb_e8(climb_e8_of(aircraft));
    p.set_track_c9(static_cast<uint16_t>(aircraft.track_deg * 512 / 360) & 0x1FF);
    p.SourceIntegrity = 3;
    p.DesignAssurance = 2;
    p.NavigIntegrity = 11;
    p.HorizAccuracy = 6;
    p.VertAccuracy = 3;
    p.VelocityAccuracy = 2;
    p.scramble();
    p.set_crc();
    return protocol::encode_mband(protocol::kAdslSyncWord, p.Data, protocol::kAdslFrameBytes,
                                  chips);
}

// An ALP-TAS aircraft carries a FLARM-taxonomy address, so the address type says
// so and the receiver reports it as one.
size_t alptas_burst(const VirtualAircraft& aircraft, uint32_t utc_s, int32_t altitude_m,
                    int32_t latitude_1e7, int32_t longitude_1e7, uint8_t* chips) {
    model::AircraftObservation observation{};
    observation.address = aircraft.address;
    observation.address_mapping_table = 0x06;
    observation.aircraft_category = 4;
    observation.flight_state = 2;
    observation.latitude_1e7 = latitude_1e7;
    observation.longitude_1e7 = longitude_1e7;
    observation.altitude_m = altitude_m;
    observation.speed_q = static_cast<uint16_t>(aircraft.speed_mps * 4);
    observation.climb_e8 = climb_e8_of(aircraft);
    observation.track_c9 = static_cast<uint16_t>(aircraft.track_deg * 512 / 360) & 0x1FF;
    observation.speed_valid = true;
    observation.climb_valid = true;
    observation.position_valid = true;

    uint8_t frame[protocol::kAlptasFrameBytes] = {0};
    if (protocol::alptas_encode(frame, observation, utc_s, latitude_1e7, longitude_1e7) !=
        Status::Ok)
        return 0;
    return protocol::encode_mband(protocol::kAlptasSyncWord, frame, protocol::kAlptasFrameBytes,
                                  chips);
}

}  // namespace

void World::transmit(VirtualAircraft& aircraft, uint64_t epoch_us, const model::Ownship& own) {
    const double cos_latitude = std::cos(origin_latitude_1e7_ / 1e7 * kPi / 180.0);
    int32_t latitude_1e7 = origin_latitude_1e7_ +
                           static_cast<int32_t>(aircraft.north_m * 1e7 / kMetresPerDegreeLatitude);
    int32_t longitude_1e7 = origin_longitude_1e7_;
    if (cos_latitude > 0.01)
        longitude_1e7 +=
            static_cast<int32_t>(aircraft.east_m * 1e7 / (kMetresPerDegreeLatitude * cos_latitude));

    // ALP-TAS keys its frames on the UTC second, so a transmitter has to use the
    // second the receiver is in, not the simulation's own count of seconds.
    const uint32_t utc_s = own.utc_valid ? own.utc_s : static_cast<uint32_t>(epoch_us / 1000000);
    const int32_t altitude_m =
        to_metres(Millimetres(own.altitude_hae_mm)).v + static_cast<int32_t>(aircraft.up_m);
    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length =
        aircraft.system == protocol::System::Alptas
            ? alptas_burst(aircraft, utc_s, altitude_m, latitude_1e7, longitude_1e7, chips)
            : adsl_burst(aircraft, utc_s, altitude_m, latitude_1e7, longitude_1e7, chips);
    if (chip_length == 0) return;

    int slot = aircraft.slot;
    if (slot < 0)
        slot = aircraft.phase_ms >= 0 ? (timing::Scheduler::slot_of(aircraft.phase_ms) == 1 ? 1 : 0)
                                      : static_cast<int>(aircraft.transmissions & 1u);
    // A pinned phase inside slot 1's tail belongs to the slot that opened in the
    // previous second, so its burst is 800..1200 ms after that second, not this.
    const bool tail = aircraft.phase_ms >= 0 && aircraft.phase_ms < timing::kSlot1WrapMs;
    int phase_ms = aircraft.phase_ms;
    if (phase_ms < 0) {
        const int first_ms = timing::Scheduler::slot_start(slot) + timing::kJitterGuardMs;
        const int last_ms = timing::Scheduler::slot_end(slot) - timing::kJitterGuardMs -
                            static_cast<int>(Air::kAirTimeUs / 1000);
        phase_ms =
            first_ms +
            static_cast<int>((aircraft.address * 2654435761u + aircraft.transmissions * 40503u) %
                             static_cast<uint32_t>(last_ms - first_ms));
    }
    aircraft.transmissions++;

    const double north_m = aircraft.north_m - own_north_m();
    const double east_m = aircraft.east_m - own_east_m();
    const double range_m = std::sqrt(north_m * north_m + east_m * east_m);
    air_.emit(epoch_us + static_cast<uint64_t>(tail ? phase_ms + 1000 : phase_ms) * 1000,
              timing::Scheduler::slot_frequency_hz(slot), chips, static_cast<uint8_t>(chip_length),
              rssi_at(range_m));
    name_itself(aircraft, epoch_us, utc_s, range_m);
}

// What a skyBlip or an OGN tracker adds to its position bursts: its registration,
// in slot 1's tail, in the one second of ten its own address owns.
void World::name_itself(const VirtualAircraft& aircraft, uint64_t epoch_us, uint32_t utc_s,
                        double range_m) {
    if (aircraft.callsign[0] == 0) return;
    const uint32_t period_s = timing::Transmitter::kCallsignPeriodS;
    if (utc_s % period_s != aircraft.address % period_s) return;

    uint8_t chips[protocol::kTxChipBytes] = {0};
    const size_t chip_length = callsign_burst(aircraft, chips);
    if (chip_length == 0) return;
    const int first_ms = timing::kCallsignStartMs;
    const int last_ms =
        timing::kCallsignEndMs - timing::kJitterGuardMs - static_cast<int>(Air::kAirTimeUs / 1000);
    const int phase_ms = first_ms + static_cast<int>((aircraft.address * 2654435761u) %
                                                     static_cast<uint32_t>(last_ms - first_ms));
    air_.emit(epoch_us + static_cast<uint64_t>(phase_ms) * 1000, timing::kMband1Hz, chips,
              static_cast<uint8_t>(chip_length), rssi_at(range_m));
}

void World::send_config(const char* json) {
    if (!platform_.link().up()) connect_companion();
    events::RxFrame frame{};
    frame.session_id = platform_.link().session_id();
    frame.endpoint = events::Endpoint::Config;
    const size_t length = std::strlen(json);
    frame.bytes = static_cast<uint16_t>(length < frame.data.size() ? length : frame.data.size());
    std::memcpy(frame.data.data(), json, frame.bytes);
    platform_.link().push_rx(frame);
}

void World::load(const Scenario& scenario) {
    scenario_ = scenario;
    next_event_ = 0;
    failures_ = 0;
    failure_[0] = 0;
    started_ = false;
    origin_valid_ = false;

    gnss().latitude_1e7 = scenario.latitude_1e7;
    gnss().longitude_1e7 = scenario.longitude_1e7;
    gnss().altitude_m = scenario.altitude_m;
    set_speed_kt(scenario.speed_kt);
    gnss().track_deg = scenario.track_deg;
    gnss().turn_dps = scenario.turn_dps;

    clear_aircraft();
    for (const ScenarioAircraft& aircraft : scenario.aircraft)
        add_aircraft(aircraft.north_m, aircraft.east_m, aircraft.up_m, aircraft.speed_mps,
                     aircraft.track_deg, aircraft.phase_ms, aircraft.slot, aircraft.system,
                     aircraft.turn_dps, aircraft.climb_mps);
}

void World::apply_events(uint32_t now_ms, const bus::State& state) {
    const uint32_t elapsed_ms = now_ms - start_ms_;
    while (next_event_ < scenario_.events.size() &&
           scenario_.events[next_event_].at_ms <= elapsed_ms) {
        const ScenarioEvent& e = scenario_.events[next_event_++];
        switch (e.kind) {
            case EventKind::Fix: set_fix(e.value != 0); break;
            case EventKind::Pps: set_pps_locked(e.value != 0); break;
            case EventKind::Barometer: platform_.barometer().fitted = e.value != 0; break;
            case EventKind::BatteryMv: set_battery_mv(static_cast<int32_t>(e.value)); break;
            case EventKind::ExternalPower: set_external_power(e.value != 0); break;
            case EventKind::Button: press_button(); break;
            case EventKind::Altitude: set_altitude_m(static_cast<int32_t>(e.value)); break;
            case EventKind::Speed: set_speed_kt(static_cast<int32_t>(e.value)); break;
            case EventKind::Track: set_track_deg(static_cast<int32_t>(e.value)); break;
            case EventKind::Aircraft: add_threat(); break;
            case EventKind::ExpectAlarmMin:
                if (traffic::to_number(state.alarm_level) < e.value)
                    fail("alarm level below expectation");
                break;
            case EventKind::ExpectTrafficMin:
                if (state.traffic.count() < e.value) fail("traffic count below expectation");
                break;
        }
    }
}

void World::fail(const char* what) {
    failures_++;
    if (failure_[0] == 0) std::snprintf(failure_, sizeof(failure_), "%s", what);
}

}  // namespace skyblip::simulator
