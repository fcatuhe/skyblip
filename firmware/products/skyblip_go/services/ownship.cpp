#include "products/skyblip_go/services/ownship.h"

#include "core/diagnostics/payload.h"
#include "core/diagnostics/profile.h"
#include "core/events/sensor.h"
#include "core/flight/arc.h"
#include "core/flight/atmosphere.h"
#include "core/flight/turn.h"
#include "core/model/ownship.h"
#include "core/timing/slot.h"
#include "core/timing/slot_timing_stats.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::go {

flight::FlightState OwnshipService::flight_state_from(const model::Ownship& own, uint32_t now_ms) {
    flight::FlightSample sample{};
    sample.speed_mm_s = own.speed_mm_s;
    sample.hdop_e2 = own.hdop_e2;
    sample.fix_valid = own.fix_valid;
    return flight_.update(sample, now_ms);
}

void OwnshipService::tick(uint32_t now_ms) {
    barometer_active_ = barometer_heard_within_max_age(now_ms);
    gnss::GnssSolution solution{};
    while (context_.bus.gnss.pop(solution)) apply_solution(solution, now_ms);

    events::BarometerSample sample{};
    while (context_.bus.barometer.pop(sample)) apply_barometer(sample, now_ms);

    events::AccelerometerSample specific_force{};
    while (context_.bus.accelerometer.pop(specific_force)) apply_accelerometer(specific_force);
    publish_inertial(now_ms);
    record_motion(now_ms);
    record_pps(now_ms);

    timer_.update(flight_.state(), now_ms);
    const flight::FlightTimer& timer = timer_on_glass(now_ms);
    context_.state.flight.confirmed_state = ground_.state();
    context_.state.flight.seconds = timer.seconds();
    context_.state.flight.time_valid = timer.flown();
    context_.state.flight.running = timer.running();
    context_.state.flight.rolling = flight_.rolling();

    acquisition_.tick(now_ms);
    context_.state.gnss.stage = acquisition_.stage();
    context_.state.gnss.stage_s = acquisition_.stage_ms(now_ms) / 1000;

    context_.state.barometer.active = barometer_active_;
    context_.state.own.tx_settled = first_fix_.settled(now_ms);
    context_.state.own.fix_acquired = first_fix_.take_acquired();
}

// INFO: fc 05oct26 a flight run flies the glass alone: air, flight log and gates read flight_
const flight::FlightTimer& OwnshipService::timer_on_glass(uint32_t now_ms) {
    if (simulating_flight()) {
        simulated_.update(flight::FlightState::Airborne, now_ms);
        return simulated_;
    }
    simulated_ = {};
    return timer_;
}

bool OwnshipService::simulating_flight() const {
    return context_.diagnostics.armed() &&
           diagnostics::simulates_flight(context_.diagnostics.profile());
}

void OwnshipService::apply_solution(const gnss::GnssSolution& solution, uint32_t now_ms) {
    model::Ownship& own = context_.state.own;
    const model::Ownship previous = own;
    context_.state.flight.gnss_solutions++;
    acquisition_.observe(solution, now_ms);
    context_.state.gnss.fix_mode = solution.fix_mode;

    own.fix_valid = solution.fix_valid;
    own.utc_valid = solution.utc_valid;
    own.latitude_1e7 = solution.latitude_1e7;
    own.longitude_1e7 = solution.longitude_1e7;
    own.altitude_hae_mm = solution.altitude_hae_mm;
    own.altitude_msl_mm = solution.altitude_msl_mm;
    own.geoid_measured = solution.geoid_measured;
    own.speed_mm_s = solution.speed_mm_s;
    own.track_cdeg = solution.track_cdeg;
    own.hdop_e2 = solution.hdop_e2;
    own.vdop_e2 = solution.vdop_e2;
    own.utc_s = solution.utc_s;
    own.fix_at_ms = solution_instant(solution, now_ms);
    own.satellites = solution.satellites;
    own.aircraft_category = settings_.aircraft_type;

    context_.state.clock.utc_valid = solution.utc_valid;
    anchor_utc_s(solution);
    publish_solution_phase(now_ms);

    int32_t mm_s = 0;
    const bool height_fixed = own.fix_valid && height_solved(own);
    if (!height_fixed) gnss_reference_ms_ = 0;
    const bool climb_valid =
        height_fixed &&
        climb_from_altitude_mm(solution.altitude_hae_mm, now_ms, kGnssClimbWindowMs,
                               gnss_reference_altitude_mm_, gnss_reference_ms_, mm_s);
    if (!barometer_active_) {
        if (climb_valid) adopt_climb(mm_s, now_ms);
        if (!height_fixed) withdraw_climb();
    }

    const flight::FlightState declared = flight_state_from(own, now_ms);
    own.flight_state = static_cast<uint8_t>(declared);
    ground_.update(declared);
    update_turn_rate(now_ms);
    update_residual(previous);
    first_fix_.observe(convergence_of(own), now_ms);
    own.tx_settled = first_fix_.settled(now_ms);
    record_gnss(solution, now_ms);
    record_flight(now_ms);
}

void OwnshipService::record_gnss(const gnss::GnssSolution& solution, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    const model::Ownship& own = context_.state.own;
    const bus::GnssState& status = context_.state.gnss;
    diagnostics::Gnss sample{};
    sample.solution_phase_ms = status.solution_phase_ms;
    sample.residual_m = own.prediction_residual_m;
    sample.hdop_e2 = own.hdop_e2;
    sample.vdop_e2 = own.vdop_e2;
    sample.stage_s = acquisition_.stage_ms(now_ms) / 1000;
    sample.satellites = own.satellites;
    sample.satellites_in_view = static_cast<uint8_t>(status.sky.count());
    sample.fix_mode = solution.fix_mode;
    sample.reject = status.reject;
    sample.stage = acquisition_.stage();
    sample.fix_valid = own.fix_valid;
    sample.residual_valid = own.prediction_residual_valid;
    sample.pps_locked = context_.state.clock.pps_locked;
    sample.geoid_measured = own.geoid_measured;
    sample.tx_settled = own.tx_settled;
    sample.levels_fresh = status.levels_fresh;
    context_.diagnostics.record(sample, context_.instant(now_ms));
}

void OwnshipService::record_pps(uint32_t now_ms) {
    const timing::ClockState& clock = context_.state.clock;
    const bool edged = clock.pps_edge_us != pps_edge_us_;
    if (!edged && now_ms - pps_recorded_ms_ < kPpsRecordPeriodMs) return;
    const uint64_t previous = pps_edge_us_;
    pps_edge_us_ = clock.pps_edge_us;
    pps_recorded_ms_ = now_ms;
    if (!context_.diagnostics.armed()) return;

    const timing::SlotTimingStats& stats = context_.state.rf.timing_stats;
    diagnostics::Pps sample{};
    if (edged && previous != 0)
        sample.interval_us = static_cast<uint32_t>(clock.pps_edge_us - previous);
    if (sample.interval_us != 0)
        sample.error_us = static_cast<int32_t>(static_cast<int64_t>(sample.interval_us) -
                                               timing::SlotTimingStats::kNominalSecondUs);
    sample.samples = stats.pps_samples();
    sample.holdover_events = stats.holdover_events();
    sample.since_edge_ms = diagnostics::clamp_u16(clock.since_edge_ms);
    sample.locked = clock.pps_locked;
    sample.utc_valid = clock.utc_valid;
    context_.diagnostics.record(sample, context_.instant(now_ms));
}

void OwnshipService::record_flight(uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    const model::Ownship& own = context_.state.own;
    diagnostics::Flight sample{};
    sample.speed_mm_s = own.speed_mm_s;
    sample.climb_mm_s = own.climb_mm_s;
    sample.altitude_msl_m = static_cast<int16_t>(own.altitude_msl_mm / 1000);
    sample.hdop_e2 = own.hdop_e2;
    sample.vdop_e2 = own.vdop_e2;
    sample.declared = static_cast<flight::FlightState>(own.flight_state);
    sample.confirmed = ground_.state();
    sample.fix_valid = own.fix_valid;
    sample.rolling = flight_.rolling();
    sample.climb_valid = own.climb_valid;
    sample.tx_settled = own.tx_settled;
    context_.diagnostics.record(sample, context_.instant(now_ms));
}

gnss::Convergence OwnshipService::convergence_of(const model::Ownship& own) {
    gnss::Convergence c{};
    c.fix_valid = own.fix_valid;
    c.residual_valid = own.prediction_residual_valid;
    c.height_solved = height_solved(own);
    c.residual_m = own.prediction_residual_m;
    return c;
}

bool OwnshipService::height_solved(const model::Ownship& own) { return own.vdop_e2 != 0; }

void OwnshipService::update_residual(const model::Ownship& previous) {
    model::Ownship& own = context_.state.own;
    if (!previous.fix_valid || !own.fix_valid) {
        own.prediction_residual_valid = false;
        return;
    }
    const int32_t dt_ms = static_cast<int32_t>(own.fix_at_ms - previous.fix_at_ms);
    if (dt_ms <= 0) {
        own.prediction_residual_valid = false;
        return;
    }
    const flight::Prediction p = flight::extrapolate(previous, dt_ms);
    if (!p.valid) {
        own.prediction_residual_valid = false;
        return;
    }
    const uint32_t resid =
        flight::prediction_residual_m(p, own.latitude_1e7, own.longitude_1e7, own.altitude_hae_mm);
    own.prediction_residual_m = resid > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(resid);
    own.prediction_residual_valid = true;
}

// INFO: fc 16sep26 a sentence names the second its own edge opened, and only that edge dates it
void OwnshipService::anchor_utc_s(const gnss::GnssSolution& solution) {
    timing::ClockState& clock = context_.state.clock;
    if (!dates_the_edge(solution, clock)) return;
    if (context_.state.own.fix_at_ms != static_cast<uint32_t>(clock.pps_edge_us / 1000)) return;
    clock.utc_s = solution.utc_s;
    clock.utc_edge_us = clock.pps_edge_us;
}

bool OwnshipService::dates_the_edge(const gnss::GnssSolution& solution,
                                    const timing::ClockState& clock) {
    if (!solution.utc_valid || !clock.pps_locked) return false;
    return solution.fix_valid || clock.utc_s == 0;
}

// INFO: fc 19sep26 a pass late at worst, and the deadline it is read against is 450 ms wide
void OwnshipService::publish_solution_phase(uint32_t now_ms) {
    const timing::ClockState& clock = context_.state.clock;
    bus::GnssState& status = context_.state.gnss;
    status.solution_phase_valid = clock.pps_locked;
    if (!status.solution_phase_valid) return;
    const uint32_t edge_ms = static_cast<uint32_t>(clock.pps_edge_us / 1000);
    status.solution_phase_ms = static_cast<uint16_t>((now_ms - edge_ms) % 1000);
}

// INFO: fc 13sep26 the latched edge dates the solution exactly, the estimate only when it is lost
uint32_t OwnshipService::solution_instant(const gnss::GnssSolution& solution,
                                          uint32_t now_ms) const {
    const timing::ClockState& clock = context_.state.clock;
    return gnss::solution_instant_ms(
        solution, now_ms, static_cast<uint32_t>(clock.pps_edge_us / 1000), clock.pps_locked);
}

void OwnshipService::apply_barometer(const events::BarometerSample& sample, uint32_t now_ms) {
    context_.state.barometer.pressure_mpa = sample.pressure_mpa;
    context_.state.barometer.temperature_dc = sample.temperature_dc;
    context_.state.barometer.temperature_valid = sample.temperature_valid;
    const int32_t altitude_mm = flight::pressure_to_altitude_mm(sample.pressure_mpa);
    if (!barometer_heard_within_max_age(now_ms)) barometer_reference_ms_ = 0;
    barometer_heard_ms_ = now_ms == 0 ? 1 : now_ms;
    barometer_active_ = true;
    int32_t mm_s = 0;
    const bool adopted =
        climb_from_altitude_mm(altitude_mm, sample.at_ms, kBarometerClimbWindowMs,
                               barometer_reference_altitude_mm_, barometer_reference_ms_, mm_s);
    if (adopted) adopt_climb(mm_s, now_ms);
    record_barometer(sample, altitude_mm, mm_s, adopted, now_ms);
}

void OwnshipService::record_barometer(const events::BarometerSample& sample, int32_t altitude_mm,
                                      int32_t climb_mm_s, bool adopted, uint32_t now_ms) {
    if (!context_.diagnostics.armed()) return;
    diagnostics::Barometer value{};
    value.pressure_mpa = sample.pressure_mpa;
    value.altitude_mm = altitude_mm;
    value.climb_mm_s = climb_mm_s;
    value.temperature_dc = sample.temperature_dc;
    value.active = barometer_active();
    value.temperature_valid = sample.temperature_valid;
    value.climb_adopted = adopted;
    context_.diagnostics.record(value, context_.instant(now_ms));
}

void OwnshipService::record_motion(uint32_t now_ms) {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Inclinometer)) return;
    if (now_ms - motion_recorded_ms_ < kMotionRecordPeriodMs) return;
    motion_recorded_ms_ = now_ms;
    if (!context_.diagnostics.armed()) return;

    const bus::State& state = context_.state;
    diagnostics::Motion value{};
    value.slip_mg = state.slip.lateral_mg;
    value.normal_mg = state.gload.now.normal_mg;
    value.lateral_mg = state.gload.now.lateral_mg;
    value.longitudinal_mg = state.gload.now.longitudinal_mg;
    value.most_normal_mg = state.gload.most.normal_mg;
    value.least_normal_mg = state.gload.least.normal_mg;
    value.imu_error = state.imu.error;
    value.sensor_error = state.imu.sensor_error;
    value.slip_valid = state.slip.valid;
    value.gload_valid = state.gload.valid;
    value.fitted = true;
    context_.diagnostics.record(value, context_.instant(now_ms));
}

void OwnshipService::apply_accelerometer(const events::AccelerometerSample& sample) {
    const flight::SpecificForce force{sample.right_mg, sample.up_mg, sample.aft_mg};
    ball_.observe(force, sample.at_ms);
    gmeter_.observe(force, sample.at_ms);
}

void OwnshipService::publish_inertial(uint32_t now_ms) {
    context_.state.slip.valid = ball_.valid(now_ms);
    context_.state.slip.lateral_mg = ball_.damped_mg();
    context_.state.slip.measured_mg = ball_.measured_mg();

    context_.state.gload.valid = gmeter_.valid(now_ms);
    context_.state.gload.now = gmeter_.now();
    context_.state.gload.most = gmeter_.most();
    context_.state.gload.least = gmeter_.least();

    const bool flying = context_.state.flight.running;
    if (flying && !flying_) gmeter_.reset();
    flying_ = flying;
}

void OwnshipService::adopt_climb(int32_t mm_s, uint32_t now_ms) {
    model::Ownship& own = context_.state.own;
    own.climb_mm_s = mm_s;
    own.climb_valid = true;
    indicated_climb_.observe(mm_s, now_ms);
    context_.state.indicated.climb_mm_s = indicated_climb_.value();
}

void OwnshipService::withdraw_climb() {
    context_.state.own.climb_valid = false;
    indicated_climb_.reset();
}

void OwnshipService::update_turn_rate(uint32_t now_ms) {
    if (!flight_.rolling()) {
        turn_reference_ms_ = 0;
        publish_turn(0, now_ms);
        return;
    }
    const CentiDegrees track{context_.state.own.track_cdeg};
    if (turn_reference_ms_ == 0) {
        turn_reference_ms_ = now_ms == 0 ? 1 : now_ms;
        turn_reference_track_cdeg_ = track.value;
        return;
    }
    const uint32_t dt = now_ms - turn_reference_ms_;
    if (dt < flight::kTurnWindowMs) return;

    publish_turn(flight::clamped_turn_cdps(
                     flight::turn_rate_cdps(track, CentiDegrees(turn_reference_track_cdeg_), dt)),
                 now_ms);
    turn_reference_ms_ = now_ms;
    turn_reference_track_cdeg_ = track.value;
}

void OwnshipService::publish_turn(int16_t turn_cdps, uint32_t now_ms) {
    context_.state.own.turn_cdps = turn_cdps;
    indicated_turn_.observe(turn_cdps, now_ms);
    context_.state.indicated.turn_cdps = static_cast<int16_t>(indicated_turn_.value());
}

bool OwnshipService::barometer_heard_within_max_age(uint32_t now_ms) const {
    return barometer_heard_ms_ != 0 && now_ms - barometer_heard_ms_ <= kBarometerMaxAgeMs;
}

bool OwnshipService::climb_from_altitude_mm(int32_t altitude_mm, uint32_t now_ms,
                                            uint32_t window_ms, int32_t& reference_altitude_mm,
                                            uint32_t& reference_ms, int32_t& out_mm_s) {
    if (reference_ms == 0) {
        reference_ms = now_ms == 0 ? 1 : now_ms;
        reference_altitude_mm = altitude_mm;
        return false;
    }
    if (now_ms - reference_ms < window_ms) return false;

    const bool ok = flight::climb_mm_s_from_altitude(altitude_mm, reference_altitude_mm,
                                                     now_ms - reference_ms, out_mm_s);
    reference_ms = now_ms;
    reference_altitude_mm = altitude_mm;
    return ok;
}

}  // namespace skyblip::go
