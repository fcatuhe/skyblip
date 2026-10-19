#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/traffic/range.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/services/screen.h"

namespace skyblip::go {

namespace {
PpsState pps_state(const timing::ClockState& clock) {
    if (clock.pps_locked) return PpsState::Lock;
    return timing::in_pps_holdover(clock) ? PpsState::Holdover : PpsState::None;
}

int traffic_outside_formation(const traffic::TrafficTable& table) {
    int n = 0;
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* t = table.at(i);
        if (t && t->used && !t->in_formation) n++;
    }
    return n;
}
}  // namespace

void ScreenService::draw_prompt() {
    ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(prompt_);
    snapshot.detail = config_.prompt_detail();
    snapshot.timeout_s = comms::kPromptForgetMs / 1000;
    draw_confirm(framebuffer_, snapshot);
}

void ScreenService::draw_menu_page() {
    if (editor_.editing()) {
        CallsignSnapshot field;
        field.text = editor_.text();
        field.cursor = editor_.cursor();
        field.clears = callsign_press_clears(field.text, field.cursor);
        draw_callsign(framebuffer_, field);
        return;
    }
    MenuSnapshot snapshot;
    snapshot.page = editor_.page();
    snapshot.values = menu_values();
    snapshot.focus = editor_.focus();
    draw_menu(framebuffer_, snapshot);
}

RawSnapshot ScreenService::raw_snapshot(uint32_t now_ms) const {
    const bus::State& state = context_.state;
    const model::Ownship& own = state.own;
    const timing::SlotTimingStatistics& statistics = state.rf.timing_statistics;

    RawSnapshot snapshot;
    snapshot.uptime_s = now_ms / 1000;

    snapshot.gnss.health = context_.roles.gnss.health();
    snapshot.gnss.stage = state.gnss.stage;
    snapshot.gnss.pps = pps_state(state.clock);
    snapshot.gnss.pps_age_ms = state.clock.since_edge_ms;
    snapshot.gnss.solutions = state.flight.gnss_solutions;
    snapshot.gnss.utc_s = own.utc_s;
    snapshot.gnss.nav_ms = state.gnss.solution_phase_ms;
    snapshot.gnss.nav_valid = state.gnss.solution_phase_valid;
    snapshot.gnss.hdop_e2 = own.hdop_e2;
    snapshot.gnss.vdop_e2 = own.vdop_e2;
    snapshot.gnss.resid_m = own.prediction_residual_m;
    snapshot.gnss.resid_valid = own.prediction_residual_valid;
    snapshot.gnss.satellites = own.satellites;
    snapshot.gnss.in_use = static_cast<uint8_t>(state.gnss.sky.in_use());
    snapshot.gnss.fix_mode = state.gnss.fix_mode;
    snapshot.gnss.fix_valid = own.fix_valid;
    snapshot.gnss.utc_valid = own.utc_valid;
    snapshot.gnss.settled = own.tx_settled;
    snapshot.gnss.levels_live = state.gnss.levels_live;

    snapshot.radio.rx_ok = state.air.rx_ok;
    snapshot.radio.rx_bad = state.air.rx_bad;
    snapshot.radio.rx_wait = state.air.rx_wait;
    snapshot.radio.rx_type = state.air.rx_type;
    snapshot.radio.rx_unframed = state.air.rx_unframed;
    snapshot.radio.rx_miskeyed = state.air.rx_miskeyed;
    snapshot.radio.rx_noise = state.air.rx_noise;
    snapshot.radio.rx_named = state.air.rx_named;
    snapshot.radio.uplink_frames = state.air.uplink_frames;
    snapshot.radio.uplink_bad = state.air.uplink_bad;
    snapshot.radio.uplink_targets = state.air.uplink_targets;
    snapshot.radio.tx_ok = state.air.tx_ok;
    snapshot.radio.tx_lost = state.air.tx_lost;
    snapshot.radio.tx_named = state.air.tx_named;
    snapshot.radio.missed = statistics.missed();
    snapshot.radio.refused = statistics.refused();
    snapshot.radio.duty_permille = state.rf.duty_permille;
    snapshot.radio.holdover = statistics.holdover_events();
    snapshot.radio.dwell_worst_us = statistics.dwell_worst_us();
    snapshot.radio.pps_worst_us = statistics.pps_worst_us();
    snapshot.radio.tx_keyed_us = state.rf.last_tx_keyed_us;
    snapshot.radio.tx_span_us = state.rf.last_tx_span_us;
    snapshot.radio.tracked = static_cast<uint16_t>(state.traffic.count());
    snapshot.radio.alarm = traffic::to_number(state.alarm_level);
    snapshot.radio.noise_dbm = state.rf.noise_dbm;
    snapshot.radio.slot = state.rf.plan.state;
    snapshot.radio.frequency_hz = state.rf.plan.frequency_hz;
    snapshot.radio.tx_allowed = state.rf.plan.tx_allowed;
    return snapshot;
}

CaptureSnapshot ScreenService::capture_snapshot(uint32_t now_ms) const {
    CaptureSnapshot snapshot;
    snapshot.uptime_s = now_ms / 1000;
    snapshot.capture = context_.state.capture;
    snapshot.focus = capture_in_focus_;
    snapshot.running = context_.diagnostics.profile();
    snapshot.focus_keeps_s = capture_.keeps_s(capture_in_focus_);
    snapshot.arming = arming_.pressed();
    return snapshot;
}

// INFO: fc 18oct26 the unlock swaps the activation page for the radar, through a wipe like any page
bool ScreenService::unit_activated() {
    const bool activated = context_.state.activation.activated();
    if (activated_ && *activated_ != activated) change_screen();
    activated_ = activated;
    return activated;
}

void ScreenService::render(uint32_t now_ms) {
    if (answering()) {
        draw_prompt();
        return;
    }
    if (receiving_) {
        draw_receiving(framebuffer_);
        return;
    }
    if (!unit_activated()) {
        draw_activation(framebuffer_, context_.state.activation.bluetooth_tag);
        return;
    }
    if (mode_ == Mode::Menu) {
        draw_menu_page();
        return;
    }

    framebuffer_.clear(/*white=*/true);

    const model::Ownship& own = context_.state.own;
    const go::Settings& settings = settings_;

    switch (page_) {
        case Page::Radar: {
            RadarSnapshot snapshot;
            snapshot.fix_valid = own.fix_valid;
            snapshot.stage = context_.state.gnss.stage;
            snapshot.units = settings.units;
            snapshot.range_step = settings.range_step;
            snapshot.plot = settings.plot;
            snapshot.heard_count = traffic_outside_formation(context_.state.traffic);
            snapshot.track_cdeg = own.track_cdeg;
            snapshot.speed_mm_s = own.speed_mm_s;
            snapshot.turn_cdps = context_.state.indicated.turn_cdps;
            snapshot.flight_seconds = context_.state.flight.seconds;
            snapshot.flight_time_valid = context_.state.flight.time_valid;
            snapshot.flight_running = context_.state.flight.running;
            snapshot.taxiing = taxiing();
            snapshot.receiver_listening = receiver_listening();
            snapshot.alarm_flash = alarm_flash_;
            snapshot.formation_members = context_.state.formation.members;
            snapshot.battery_percent = context_.state.power.battery.percent;
            snapshot.battery_low = battery_low();
            int n = 0;
            if (own.fix_valid) {
                const model::Ownship own_now = flight::carried_to(own, now_ms);
                for (int i = 0; i < traffic::TrafficTable::kMaxTargets && n < kMaxRadarTargets;
                     i++) {
                    const traffic::Target* t = context_.state.traffic.at(i);
                    if (!t || !t->used) continue;
                    const model::AircraftObservation observation =
                        flight::carried_to(t->observation, now_ms);
                    int32_t north = 0, east = 0, up = 0;
                    if (!flight::relative_north_east_up(own_now, observation, north, east, up))
                        continue;
                    targets_[n].north_m = north;
                    targets_[n].east_m = east;
                    targets_[n].up_m = up;
                    targets_[n].alarm_level = t->alarm_level;
                    targets_[n].alarm_dismissed = t->alarm_dismissed;
                    targets_[n].climb_e8 = observation.climb_e8;
                    targets_[n].climb_valid = observation.climb_valid;
                    targets_[n].speed_mm_s =
                        observation.speed_valid
                            ? to_mm_s(QuarterMetresPerSecond(observation.speed_q)).value
                            : 0;
                    targets_[n].track_cdeg = to_centi_degrees(Cordic9(observation.track_c9)).value;
                    targets_[n].turn_cdps = static_cast<int16_t>(t->turn.rate_dps * 100);
                    targets_[n].turn_valid = t->turn.valid;
                    targets_[n].in_formation = t->in_formation;
                    n++;
                }
            }
            snapshot.target_count = n;
            snapshot.targets = targets_;
            draw_radar(framebuffer_, snapshot);
            break;
        }
        case Page::SixPack: {
            SixPackSnapshot snapshot;
            snapshot.fix_valid = own.fix_valid;
            snapshot.units = settings.units;
            snapshot.speed_kt = to_knots(MillimetresPerSecond(own.speed_mm_s)).value;
            snapshot.altitude_ft = to_feet(Millimetres(own.altitude_hae_mm)).value;
            snapshot.climb_fpm = climb_fpm();
            snapshot.climb_valid = climb_measured();
            snapshot.track_deg = to_degrees(CentiDegrees(own.track_cdeg)).value;
            snapshot.turn_cdps = context_.state.indicated.turn_cdps;
            snapshot.battery_percent = context_.state.power.battery.percent;
            snapshot.battery_valid = context_.state.power.battery.valid;
            snapshot.charging = context_.state.power.battery.charging;
            snapshot.battery_mv = context_.state.power.battery.sample_mv;
            snapshot.inclinometer_fitted =
                ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snapshot.lateral_valid = context_.state.slip.valid;
            snapshot.lateral_mg = context_.state.slip.lateral_mg;
            draw_sixpack(framebuffer_, snapshot);
            break;
        }
        case Page::Satellites: {
            SatellitesSnapshot snapshot;
            snapshot.fix_valid = own.fix_valid;
            snapshot.levels_live = context_.state.gnss.levels_live;
            snapshot.stage = context_.state.gnss.stage;
            snapshot.stage_s = context_.state.gnss.stage_s;
            snapshot.satellites = own.satellites;
            snapshot.hdop_e2 = own.hdop_e2;
            snapshot.vdop_e2 = own.vdop_e2;
            snapshot.nav_ms = context_.state.gnss.solution_phase_ms;
            snapshot.nav_valid = context_.state.gnss.solution_phase_valid;
            snapshot.health = context_.roles.gnss.health();
            snapshot.sky = &context_.state.gnss.sky;
            draw_satellites(framebuffer_, snapshot);
            break;
        }
        case Page::Nearby: {
            NearbySnapshot snapshot;
            snapshot.own_address = context_.state.activation.address();
            snapshot.own_callsign = settings.callsign;
            snapshot.fix_valid = own.fix_valid;
            snapshot.units = settings.units;
            snapshot.heard_count = context_.state.traffic.count();
            snapshot.row_count = traffic::rank_by_range(
                context_.state.traffic, context_.state.callsigns, own, nearby_rows_, kNearbyRows);
            snapshot.rows = nearby_rows_;
            draw_nearby(framebuffer_, snapshot);
            break;
        }
        case Page::SelfTest: {
            // INFO: fc 18oct26 the parts are boot's, the address now's: an unlock needs no reboot
            SelfTestSnapshot now = self_test_;
            now.address = context_.state.activation.address();
            draw_self_test(framebuffer_, now);
            break;
        }
        case Page::GMeter: {
            GMeterSnapshot snapshot;
            snapshot.inclinometer_fitted =
                ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snapshot.gload_valid = context_.state.gload.valid;
            snapshot.now = context_.state.gload.now;
            snapshot.most = context_.state.gload.most;
            snapshot.least = context_.state.gload.least;
            draw_gmeter(framebuffer_, snapshot);
            break;
        }
        case Page::RadioLog: {
            RadioLogSnapshot snapshot;
            snapshot.gnss.fix_valid = own.fix_valid;
            snapshot.gnss.satellites = own.satellites;
            snapshot.gnss.pps = pps_state(context_.state.clock);
            snapshot.gnss.pps_age_s =
                static_cast<uint16_t>(context_.state.clock.since_edge_ms / 1000);
            snapshot.rx_ok = context_.state.air.rx_ok;
            snapshot.tx_ok = context_.state.air.tx_ok;
            snapshot.rx_noise = context_.state.air.rx_noise;
            snapshot.noise_dbm = context_.state.rf.noise_dbm;
            snapshot.row_count = context_.state.radio_log.count();
            snapshot.log = &context_.state.radio_log;
            draw_radio_log(framebuffer_, snapshot);
            break;
        }
        case Page::Raw: {
            draw_raw(framebuffer_, raw_snapshot(now_ms));
            break;
        }
        case Page::Capture: {
            draw_capture(framebuffer_, capture_snapshot(now_ms));
            break;
        }
        case Page::Status:
        default: {
            StatusSnapshot snapshot;
            snapshot.address = context_.state.activation.address();
            snapshot.callsign = settings.callsign;
            snapshot.fix_valid = own.fix_valid;
            snapshot.utc_valid = own.utc_valid;
            snapshot.transmitting = timing::ownship_transmits(own, context_.state.clock,
                                                              context_.state.activation.address());
            snapshot.satellites = own.satellites;
            snapshot.stage = context_.state.gnss.stage;
            snapshot.stage_s = context_.state.gnss.stage_s;
            snapshot.fix_mode = context_.state.gnss.fix_mode;
            snapshot.latitude_1e7 = own.latitude_1e7;
            snapshot.longitude_1e7 = own.longitude_1e7;
            snapshot.altitude_mm = own.altitude_hae_mm;
            snapshot.speed_mm_s = own.speed_mm_s;
            snapshot.track_cdeg = own.track_cdeg;
            snapshot.climb_mm_s = own.climb_mm_s;
            snapshot.utc_s = own.utc_s;
            snapshot.heard_count = context_.state.traffic.count();
            snapshot.imu_stage = context_.state.imu.stage;
            snapshot.imu_fault = context_.state.imu.fault;
            snapshot.imu_fifo_bytes = context_.state.imu.fifo_bytes;
            snapshot.imu_unparsed = context_.state.imu.unparsed_events;
            snapshot.imu_error = context_.state.imu.error;
            snapshot.imu_interrupt = context_.state.imu.interrupt;
            snapshot.imu_meta = context_.state.imu.meta_event;
            snapshot.imu_sensor_error = context_.state.imu.sensor_error;
            snapshot.imu_errored_sensor = context_.state.imu.errored_sensor;
            snapshot.slip_valid = context_.state.slip.valid;
            snapshot.slip_mg = context_.state.slip.measured_mg;
            snapshot.barometer_valid = context_.state.barometer.active;
            snapshot.battery_valid = context_.state.power.battery.valid;
            snapshot.battery_mv = context_.state.power.battery.sample_mv;
            snapshot.battery_percent = context_.state.power.battery.percent;
            snapshot.charging = context_.state.power.battery.charging;
            snapshot.charge = context_.state.power.charge;
            snapshot.battery_low = battery_low();
            snapshot.pressure_mpa = context_.state.barometer.pressure_mpa;
            if (context_.state.barometer.active)
                snapshot.altitude_standard_mm =
                    flight::pressure_to_altitude_mm(context_.state.barometer.pressure_mpa);
            draw_status(framebuffer_, snapshot);
            break;
        }
    }
}

}  // namespace skyblip::go
