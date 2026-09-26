#include "products/skyblip_go/services/ownship.h"

#include "core/diag/payload.h"
#include "core/events/sensor.h"
#include "core/flight/arc.h"
#include "core/flight/atmosphere.h"
#include "core/flight/turn.h"
#include "core/model/ownship.h"
#include "core/timing/slot.h"
#include "core/timing/timing_stats.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::go {

flight::FlightState OwnshipService::flight_state_from(const model::OwnState& own, uint32_t now_ms) {
    flight::FlightSample sample{};
    sample.speed_mm_s = own.speed_mm_s;
    sample.hdop_e2 = own.hdop_e2;
    sample.fix_valid = own.fix_valid;
    return flight_.update(sample, now_ms);
}

void OwnshipService::tick(uint32_t now_ms) {
    baro_live_ = baro_heard_within_max_age(now_ms);
    gnss::GnssSolution solution{};
    while (context_.bus.gnss.pop(solution)) apply_solution(solution, now_ms);

    events::BaroSample sample{};
    while (context_.bus.baro.pop(sample)) apply_baro(sample, now_ms);

    events::AccelSample specific_force{};
    while (context_.bus.accel.pop(specific_force)) apply_accel(specific_force);
    publish_inertial(now_ms);
    record_motion(now_ms);
    record_pps(now_ms);

    timer_.update(flight_.state(), now_ms);
    context_.state.flight.confirmed_state = ground_.state();
    context_.state.flight.seconds = timer_.seconds();
    context_.state.flight.time_valid = timer_.flown();
    context_.state.flight.running = timer_.running();
    context_.state.flight.rolling = flight_.rolling();

    acquisition_.tick(now_ms);
    context_.state.gnss.stage = acquisition_.stage();
    context_.state.gnss.stage_s = acquisition_.stage_ms(now_ms) / 1000;

    context_.state.baro.active = baro_live_;
    context_.state.own.tx_settled = settle_.settled(now_ms);
    context_.state.own.fix_acquired = settle_.take_acquired();
}

void OwnshipService::apply_solution(const gnss::GnssSolution& solution, uint32_t now_ms) {
    model::OwnState& own = context_.state.own;
    const model::OwnState previous = own;
    context_.state.flight.gnss_solutions++;
    acquisition_.observe(solution, now_ms);
    context_.state.gnss.fix_mode = solution.fix_mode;

    own.fix_valid = solution.fix_valid;
    own.utc_valid = solution.utc_valid;
    own.lat_1e7 = solution.lat_1e7;
    own.lon_1e7 = solution.lon_1e7;
    own.alt_mm = solution.alt_mm;
    own.alt_msl_mm = solution.alt_msl_mm;
    own.geoid_separation_measured = solution.geoid_separation_measured;
    own.speed_mm_s = solution.speed_mm_s;
    own.track_cdeg = solution.track_cdeg;
    own.hdop_e2 = solution.hdop_e2;
    own.vdop_e2 = solution.vdop_e2;
    own.utc = solution.utc;
    own.fix_ms = solution_instant(solution, now_ms);
    own.sats = solution.sats;
    own.aircraft_cat = settings_.aircraft_type;

    context_.state.clock.utc_valid = solution.utc_valid;
    anchor_utc(solution);
    publish_solution_phase(now_ms);

    int32_t mm_s = 0;
    const bool height_fixed = own.fix_valid && height_solved(own);
    if (!height_fixed) vs_ref_ms_ = 0;
    const bool have = height_fixed && vs_from_alt_mm(solution.alt_mm, now_ms, kGnssVsWindowMs,
                                                     vs_ref_alt_mm_, vs_ref_ms_, mm_s);
    if (!baro_live_) {
        if (have) adopt_climb(mm_s);
        if (!height_fixed) own.climb_valid = false;
    }

    const flight::FlightState declared = flight_state_from(own, now_ms);
    own.flight_state = static_cast<uint8_t>(declared);
    ground_.update(declared);
    update_turn_rate(now_ms);
    update_residual(previous);
    settle_.update(convergence_of(own), now_ms);
    own.tx_settled = settle_.settled(now_ms);
    record_gnss(solution, now_ms);
    record_flight(now_ms);
}

void OwnshipService::record_gnss(const gnss::GnssSolution& solution, uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    const model::OwnState& own = context_.state.own;
    const bus::GnssStatus& status = context_.state.gnss;
    diag::Gnss sample{};
    sample.nav_ms = status.solution_phase_ms;
    sample.resid_m = own.pred_resid_m;
    sample.hdop_e2 = own.hdop_e2;
    sample.vdop_e2 = own.vdop_e2;
    sample.stage_s = acquisition_.stage_ms(now_ms) / 1000;
    sample.sats = own.sats;
    sample.sats_in_view = static_cast<uint8_t>(status.sky.count());
    sample.fix_mode = solution.fix_mode;
    sample.reject = status.reject;
    sample.stage = acquisition_.stage();
    sample.fix_valid = own.fix_valid;
    sample.resid_valid = own.pred_resid_valid;
    sample.pps_locked = context_.state.clock.pps_locked;
    sample.geoid_measured = own.geoid_separation_measured;
    sample.tx_settled = own.tx_settled;
    context_.diag.record(sample, context_.instant(now_ms));
}

void OwnshipService::record_pps(uint32_t now_ms) {
    const timing::ClockState& clock = context_.state.clock;
    const bool edged = clock.pps_edge_us != pps_edge_us_;
    if (!edged && now_ms - pps_recorded_ms_ < kPpsRecordPeriodMs) return;
    const uint64_t previous = pps_edge_us_;
    pps_edge_us_ = clock.pps_edge_us;
    pps_recorded_ms_ = now_ms;
    if (!context_.diag.armed()) return;

    const timing::SlotTimingStats& stats = context_.state.rf.timing_stats;
    diag::Pps sample{};
    if (edged && previous != 0)
        sample.interval_us = static_cast<uint32_t>(clock.pps_edge_us - previous);
    if (sample.interval_us != 0)
        sample.error_us = static_cast<int32_t>(static_cast<int64_t>(sample.interval_us) -
                                               timing::SlotTimingStats::kNominalSecondUs);
    sample.samples = stats.pps_samples();
    sample.holdover_events = stats.holdover_events();
    sample.since_edge_ms = diag::clamp_u16(clock.ms_since_pps);
    sample.locked = clock.pps_locked;
    sample.utc_valid = clock.utc_valid;
    context_.diag.record(sample, context_.instant(now_ms));
}

void OwnshipService::record_flight(uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    const model::OwnState& own = context_.state.own;
    diag::Flight sample{};
    sample.speed_mm_s = own.speed_mm_s;
    sample.climb_mm_s = own.climb_mm_s;
    sample.alt_msl_m = static_cast<int16_t>(own.alt_msl_mm / 1000);
    sample.hdop_e2 = own.hdop_e2;
    sample.vdop_e2 = own.vdop_e2;
    sample.declared = static_cast<flight::FlightState>(own.flight_state);
    sample.confirmed = ground_.state();
    sample.fix_valid = own.fix_valid;
    sample.rolling = flight_.rolling();
    sample.climb_valid = own.climb_valid;
    sample.tx_settled = own.tx_settled;
    context_.diag.record(sample, context_.instant(now_ms));
}

gnss::Convergence OwnshipService::convergence_of(const model::OwnState& own) {
    gnss::Convergence c{};
    c.fix_valid = own.fix_valid;
    c.resid_valid = own.pred_resid_valid;
    c.height_solved = height_solved(own);
    c.resid_m = own.pred_resid_m;
    return c;
}

bool OwnshipService::height_solved(const model::OwnState& own) { return own.vdop_e2 != 0; }

void OwnshipService::update_residual(const model::OwnState& previous) {
    model::OwnState& own = context_.state.own;
    if (!previous.fix_valid || !own.fix_valid) {
        own.pred_resid_valid = false;
        return;
    }
    const int32_t dt_ms = static_cast<int32_t>(own.fix_ms - previous.fix_ms);
    if (dt_ms <= 0) {
        own.pred_resid_valid = false;
        return;
    }
    const flight::Prediction p = flight::extrapolate(previous, dt_ms);
    if (!p.valid) {
        own.pred_resid_valid = false;
        return;
    }
    const uint32_t resid = flight::prediction_residual_m(p, own.lat_1e7, own.lon_1e7, own.alt_mm);
    own.pred_resid_m = resid > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(resid);
    own.pred_resid_valid = true;
}

// INFO: fc 16sep26 a sentence names the second its own edge opened, and only that edge dates it
void OwnshipService::anchor_utc(const gnss::GnssSolution& solution) {
    timing::ClockState& clock = context_.state.clock;
    if (!solution.utc_valid || !clock.pps_locked) return;
    if (context_.state.own.fix_ms != static_cast<uint32_t>(clock.pps_edge_us / 1000)) return;
    clock.utc_s = solution.utc;
    clock.utc_edge_us = clock.pps_edge_us;
}

// INFO: fc 19sep26 a pass late at worst, and the deadline it is read against is 450 ms wide
void OwnshipService::publish_solution_phase(uint32_t now_ms) {
    const timing::ClockState& clock = context_.state.clock;
    bus::GnssStatus& status = context_.state.gnss;
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

void OwnshipService::apply_baro(const events::BaroSample& sample, uint32_t now_ms) {
    context_.state.baro.pressure_mpa = sample.pressure_mpa;
    context_.state.baro.temperature_decicelsius = sample.temperature_decicelsius;
    context_.state.baro.temperature_valid = sample.temperature_valid;
    const int32_t alt_mm = flight::pressure_to_alt_mm(sample.pressure_mpa);
    if (!baro_heard_within_max_age(now_ms)) baro_ref_ms_ = 0;
    baro_heard_ms_ = now_ms == 0 ? 1 : now_ms;
    baro_live_ = true;
    int32_t mm_s = 0;
    const bool adopted =
        vs_from_alt_mm(alt_mm, sample.at_ms, kBaroVsWindowMs, baro_ref_alt_mm_, baro_ref_ms_, mm_s);
    if (adopted) adopt_climb(mm_s);
    record_baro(sample, alt_mm, mm_s, adopted, now_ms);
}

void OwnshipService::record_baro(const events::BaroSample& sample, int32_t alt_mm,
                                 int32_t climb_mm_s, bool adopted, uint32_t now_ms) {
    if (!context_.diag.armed()) return;
    diag::Baro value{};
    value.pressure_mpa = sample.pressure_mpa;
    value.alt_mm = alt_mm;
    value.climb_mm_s = climb_mm_s;
    value.temperature_dc = sample.temperature_decicelsius;
    value.active = baro_active();
    value.temperature_valid = sample.temperature_valid;
    value.climb_adopted = adopted;
    context_.diag.record(value, context_.instant(now_ms));
}

void OwnshipService::record_motion(uint32_t now_ms) {
    if (!ports::has(context_.roles.capabilities, ports::Capability::Inclinometer)) return;
    if (now_ms - motion_recorded_ms_ < kMotionRecordPeriodMs) return;
    motion_recorded_ms_ = now_ms;
    if (!context_.diag.armed()) return;

    const bus::State& state = context_.state;
    diag::Motion value{};
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
    context_.diag.record(value, context_.instant(now_ms));
}

void OwnshipService::apply_accel(const events::AccelSample& sample) {
    const flight::SpecificForce force{sample.right_mg, sample.up_mg, sample.aft_mg};
    ball_.update(force, sample.at_ms);
    gmeter_.observe(force, sample.at_ms);
}

void OwnshipService::publish_inertial(uint32_t now_ms) {
    context_.state.slip.valid = ball_.valid(now_ms);
    context_.state.slip.lateral_mg = ball_.mg();

    context_.state.gload.valid = gmeter_.valid(now_ms);
    context_.state.gload.now = gmeter_.now();
    context_.state.gload.most = gmeter_.most();
    context_.state.gload.least = gmeter_.least();

    const bool flying = timer_.running();
    if (flying && !flying_) gmeter_.reset();
    flying_ = flying;
}

void OwnshipService::adopt_climb(int32_t mm_s) {
    model::OwnState& own = context_.state.own;
    own.climb_mm_s = mm_s;
    own.climb_valid = true;
}

void OwnshipService::update_turn_rate(uint32_t now_ms) {
    const CentiDegrees track{context_.state.own.track_cdeg};
    if (turn_ref_ms_ == 0) {
        turn_ref_ms_ = now_ms == 0 ? 1 : now_ms;
        turn_ref_track_cdeg_ = track.v;
        return;
    }
    const uint32_t dt = now_ms - turn_ref_ms_;
    if (dt < flight::kTurnWindowMs) return;

    context_.state.own.turn_cdps = flight::clamped_turn_cdps(
        flight::turn_rate_cdps(track, CentiDegrees(turn_ref_track_cdeg_), dt));
    turn_ref_ms_ = now_ms;
    turn_ref_track_cdeg_ = track.v;
}

bool OwnshipService::baro_heard_within_max_age(uint32_t now_ms) const {
    return baro_heard_ms_ != 0 && now_ms - baro_heard_ms_ <= kBaroMaxAgeMs;
}

bool OwnshipService::vs_from_alt_mm(int32_t alt_mm, uint32_t now_ms, uint32_t window_ms,
                                    int32_t& ref_alt_mm, uint32_t& ref_ms, int32_t& out_mm_s) {
    if (ref_ms == 0) {
        ref_ms = now_ms == 0 ? 1 : now_ms;
        ref_alt_mm = alt_mm;
        return false;
    }
    if (now_ms - ref_ms < window_ms) return false;

    const bool ok = flight::climb_mm_s_from_alt(alt_mm, ref_alt_mm, now_ms - ref_ms, out_mm_s);
    ref_ms = now_ms;
    ref_alt_mm = alt_mm;
    return ok;
}

}  // namespace skyblip::go
