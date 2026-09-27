#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/protocol/nmea_out.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/units/units.h"
#include "core/util/intmath.h"
#include "products/skyblip_go/services/screen.h"

namespace skyblip::go {

namespace {
PpsState pps_state(const timing::ClockState& clock) {
    if (clock.pps_locked) return PpsState::Lock;
    return timing::in_pps_holdover(clock) ? PpsState::Holdover : PpsState::None;
}
}  // namespace

void ScreenService::draw_prompt() {
    ConfirmSnapshot snapshot;
    snapshot.title = comms::pending_title(prompt_);
    snapshot.detail = comms::pending_detail(prompt_);
    snapshot.timeout_s = comms::kConfirmWindowMs / 1000;
    draw_confirm(fb_, snapshot);
}

void ScreenService::draw_menu_page() {
    if (editor_.editing()) {
        CallsignSnapshot field;
        field.text = editor_.text();
        field.cursor = editor_.cursor();
        field.clears = callsign_press_clears(field.text, field.cursor);
        draw_callsign(fb_, field);
        return;
    }
    MenuSnapshot snapshot;
    snapshot.page = editor_.page();
    snapshot.values = menu_values();
    snapshot.focus = editor_.focus();
    draw_menu(fb_, snapshot);
}

RawSnapshot ScreenService::raw_snapshot(uint32_t now_ms) const {
    const bus::State& state = context_.state;
    const model::OwnState& own = state.own;
    const timing::SlotTimingStats& stats = state.rf.timing_stats;

    RawSnapshot snap;
    snap.uptime_s = now_ms / 1000;

    snap.gnss.health = context_.roles.gnss.health();
    snap.gnss.stage = state.gnss.stage;
    snap.gnss.pps = pps_state(state.clock);
    snap.gnss.pps_age_ms = state.clock.ms_since_pps;
    snap.gnss.solutions = state.flight.gnss_solutions;
    snap.gnss.utc = own.utc;
    snap.gnss.nav_ms = state.gnss.solution_phase_ms;
    snap.gnss.nav_valid = state.gnss.solution_phase_valid;
    snap.gnss.hdop_e2 = own.hdop_e2;
    snap.gnss.vdop_e2 = own.vdop_e2;
    snap.gnss.resid_m = own.pred_resid_m;
    snap.gnss.resid_valid = own.pred_resid_valid;
    snap.gnss.sats = own.sats;
    snap.gnss.in_use = static_cast<uint8_t>(state.gnss.sky.in_use());
    snap.gnss.fix_mode = state.gnss.fix_mode;
    snap.gnss.fix_valid = own.fix_valid;
    snap.gnss.utc_valid = own.utc_valid;
    snap.gnss.settled = own.tx_settled;
    snap.gnss.levels_live = state.gnss.levels_live;

    snap.radio.rx_ok = state.air.rx_ok;
    snap.radio.rx_bad = state.air.rx_bad;
    snap.radio.rx_wait = state.air.rx_wait;
    snap.radio.rx_type = state.air.rx_type;
    snap.radio.rx_unframed = state.air.rx_unframed;
    snap.radio.rx_miskeyed = state.air.rx_miskeyed;
    snap.radio.rx_noise = state.air.rx_noise;
    snap.radio.rx_named = state.air.rx_named;
    snap.radio.uplink_frames = state.air.uplink_frames;
    snap.radio.uplink_bad = state.air.uplink_bad;
    snap.radio.uplink_targets = state.air.uplink_targets;
    snap.radio.tx_ok = state.air.tx_ok;
    snap.radio.tx_lost = state.air.tx_lost;
    snap.radio.tx_named = state.air.tx_named;
    snap.radio.missed = stats.missed();
    snap.radio.refused = stats.refused();
    snap.radio.duty_permille = state.rf.duty_permille;
    snap.radio.holdover = stats.holdover_events();
    snap.radio.dwell_worst_us = stats.dwell_worst_us();
    snap.radio.pps_worst_us = stats.pps_worst_us();
    snap.radio.tx_keyed_us = state.rf.last_tx_keyed_us;
    snap.radio.tx_span_us = state.rf.last_tx_span_us;
    snap.radio.tracked = static_cast<uint16_t>(state.traffic.count());
    snap.radio.alarm = traffic::to_number(state.alarm_level);
    snap.radio.noise_dbm = state.rf.noise_dbm;
    snap.radio.slot = state.rf.plan.state;
    snap.radio.freq_hz = state.rf.plan.freq_hz;
    snap.radio.tx_allowed = state.rf.plan.tx_allowed;
    return snap;
}

CaptureSnapshot ScreenService::capture_snapshot(uint32_t now_ms) const {
    CaptureSnapshot snap;
    snap.uptime_s = now_ms / 1000;
    snap.capture = context_.state.capture;
    snap.focus = capture_in_focus_;
    snap.running = context_.diag.profile();
    snap.focus_keeps_s = capture_.keeps_s(capture_in_focus_);
    snap.arming = arming_.pressed();
    return snap;
}

void ScreenService::render(uint32_t now_ms) {
    if (answering()) {
        draw_prompt();
        return;
    }
    if (mode_ == Mode::Menu) {
        draw_menu_page();
        return;
    }

    fb_.clear(/*white=*/true);

    const model::OwnState& own = context_.state.own;
    const go::Settings& settings = settings_;

    switch (page_) {
        case Page::Radar: {
            RadarSnapshot snap;
            snap.fix_valid = own.fix_valid;
            snap.stage = context_.state.gnss.stage;
            snap.units = settings.units;
            snap.range_step = range_step_;
            snap.track_cdeg = own.track_cdeg;
            snap.speed_mm_s = own.speed_mm_s;
            snap.turn_cdps = context_.state.indicated.turn_cdps;
            snap.flight_seconds = context_.state.flight.seconds;
            snap.flight_time_valid = context_.state.flight.time_valid;
            snap.in_flight = context_.state.flight.running;
            snap.taxiing = taxiing();
            snap.receiver_listening = receiver_listening();
            snap.alarm_flash = alarm_flash_;
            snap.formation_members = context_.state.formation.members;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.battery_low = battery_low();
            int n = 0;
            if (own.fix_valid) {
                const model::OwnState own_now = flight::carried_to(own, now_ms);
                for (int i = 0; i < traffic::TrafficTable::kCapacity && n < kMaxRadarTargets; i++) {
                    const traffic::Target* t = context_.state.traffic.at(i);
                    if (!t || !t->used) continue;
                    const model::AircraftObs obs = flight::carried_to(t->obs, now_ms);
                    int32_t north = 0, east = 0, up = 0;
                    if (!protocol::relative_ned(own_now, obs, north, east, up)) continue;
                    targets_[n].north_m = north;
                    targets_[n].east_m = east;
                    targets_[n].up_m = up;
                    targets_[n].alarm_level = t->alarm_level;
                    targets_[n].alarm_dismissed = t->alarm_dismissed;
                    targets_[n].climb_e8 = obs.climb_e8;
                    targets_[n].climb_valid = obs.climb_valid;
                    targets_[n].speed_mm_s =
                        obs.speed_valid ? to_mm_s(QuarterMetresPerSec(obs.speed_q)).v : 0;
                    targets_[n].track_cdeg = to_centi_degrees(Cordic9(obs.track_c9)).v;
                    targets_[n].turn_cdps = static_cast<int16_t>(t->turn.dps * 100);
                    targets_[n].turn_valid = t->turn.valid;
                    targets_[n].in_formation = t->in_formation;
                    n++;
                }
            }
            snap.n_targets = n;
            snap.targets = targets_;
            draw_radar(fb_, snap);
            break;
        }
        case Page::SixPack: {
            SixPackSnapshot snap;
            snap.data_valid = own.fix_valid;
            snap.units = settings.units;
            snap.speed_kt = to_knots(MillimetresPerSec(own.speed_mm_s)).v;
            snap.alt_ft = to_feet(Millimetres(own.alt_mm)).v;
            snap.vs_fpm = climb_fpm();
            snap.vs_valid = climb_measured();
            snap.track_deg = to_degrees(CentiDegrees(own.track_cdeg)).v;
            snap.turn_cdps = context_.state.indicated.turn_cdps;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.battery_valid = context_.state.power.battery.valid;
            snap.inclinometer_fitted =
                ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snap.lateral_valid = context_.state.slip.valid;
            snap.lateral_mg = context_.state.slip.lateral_mg;
            draw_sixpack(fb_, snap);
            break;
        }
        case Page::Sats: {
            SatsSnapshot snap;
            snap.fix_valid = own.fix_valid;
            snap.levels_live = context_.state.gnss.levels_live;
            snap.stage = context_.state.gnss.stage;
            snap.stage_s = context_.state.gnss.stage_s;
            snap.sats = own.sats;
            snap.hdop_e2 = own.hdop_e2;
            snap.vdop_e2 = own.vdop_e2;
            snap.nav_ms = context_.state.gnss.solution_phase_ms;
            snap.nav_valid = context_.state.gnss.solution_phase_valid;
            snap.health = context_.roles.gnss.health();
            snap.sky = &context_.state.gnss.sky;
            draw_sats(fb_, snap);
            break;
        }
        case Page::Nearby: {
            NearbySnapshot snap;
            snap.own_addr = context_.roles.device_addr;
            snap.own_callsign = settings.callsign;
            snap.fix_valid = own.fix_valid;
            snap.units = settings.units;
            snap.n_heard = context_.state.traffic.count();
            snap.n_rows = traffic::rank_by_range(context_.state.traffic, context_.state.callsigns,
                                                 own, nearby_rows_, kNearbyRows);
            snap.rows = nearby_rows_;
            draw_nearby(fb_, snap);
            break;
        }
        case Page::SelfTest: draw_boot(fb_, self_test_); break;
        case Page::GMeter: {
            GMeterSnapshot snap;
            snap.fitted = ports::has(context_.roles.capabilities, ports::Capability::Inclinometer);
            snap.valid = context_.state.gload.valid;
            snap.now = context_.state.gload.now;
            snap.most = context_.state.gload.most;
            snap.least = context_.state.gload.least;
            draw_gmeter(fb_, snap);
            break;
        }
        case Page::RadioLog: {
            RadioLogSnapshot snap;
            snap.gnss.fix_valid = own.fix_valid;
            snap.gnss.sats = own.sats;
            snap.gnss.pps = pps_state(context_.state.clock);
            snap.gnss.pps_age_s = static_cast<uint16_t>(context_.state.clock.ms_since_pps / 1000);
            snap.rx_ok = context_.state.air.rx_ok;
            snap.tx_ok = context_.state.air.tx_ok;
            snap.noise = context_.state.air.rx_noise;
            snap.band_dbm = context_.state.rf.noise_dbm;
            snap.n_rows = context_.state.radio_log.count();
            snap.log = &context_.state.radio_log;
            draw_radio_log(fb_, snap);
            break;
        }
        case Page::Raw: {
            draw_raw(fb_, raw_snapshot(now_ms));
            break;
        }
        case Page::Capture: {
            draw_capture(fb_, capture_snapshot(now_ms));
            break;
        }
        case Page::Status:
        default: {
            StatusSnapshot snap;
            snap.device_addr = context_.roles.device_addr;
            snap.callsign = settings.callsign;
            snap.fix_valid = own.fix_valid;
            snap.utc_valid = own.utc_valid;
            snap.transmitting = timing::own_ship_transmits(own, context_.state.clock);
            snap.sats = own.sats;
            snap.stage = context_.state.gnss.stage;
            snap.stage_s = context_.state.gnss.stage_s;
            snap.fix_mode = context_.state.gnss.fix_mode;
            snap.lat_1e7 = own.lat_1e7;
            snap.lon_1e7 = own.lon_1e7;
            snap.alt_mm = own.alt_mm;
            snap.speed_mm_s = own.speed_mm_s;
            snap.track_cdeg = own.track_cdeg;
            snap.climb_mm_s = context_.state.indicated.climb_mm_s;
            snap.utc = own.utc;
            snap.n_targets = context_.state.traffic.count();
            snap.imu_stage = context_.state.imu.stage;
            snap.imu_fault = context_.state.imu.fault;
            snap.imu_fifo_bytes = context_.state.imu.fifo_bytes;
            snap.imu_unparsed = context_.state.imu.unparsed;
            snap.imu_error = context_.state.imu.error;
            snap.imu_interrupt = context_.state.imu.interrupt;
            snap.imu_meta = context_.state.imu.meta;
            snap.imu_sensor_error = context_.state.imu.sensor_error;
            snap.imu_errored_sensor = context_.state.imu.errored_sensor;
            snap.slip_valid = context_.state.slip.valid;
            snap.slip_mg = context_.state.slip.lateral_mg;
            snap.baro_valid = context_.state.baro.active;
            snap.battery_valid = context_.state.power.battery.valid;
            snap.battery_mv = context_.state.power.battery.millivolts;
            snap.battery_percent = context_.state.power.battery.percent;
            snap.charging = context_.state.power.battery.charging;
            snap.charge = context_.state.power.charge;
            snap.battery_low = battery_low();
            snap.pressure_mpa = context_.state.baro.pressure_mpa;
            if (context_.state.baro.active) {
                const uint32_t pa = div_round<uint32_t>(context_.state.baro.pressure_mpa, 1000);
                snap.alt_std_m = div_round(flight::pressure_to_alt_cm(pa), 100);
            }
            draw_status(fb_, snap);
            break;
        }
    }
}

}  // namespace skyblip::go
