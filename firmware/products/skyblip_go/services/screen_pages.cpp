#include <algorithm>

#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/model/aircraft.h"
#include "core/model/ownship.h"
#include "core/settings/address.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "core/traffic/range.h"
#include "core/units/units.h"
#include "products/skyblip_go/pages/bravo_log.h"
#include "products/skyblip_go/pages/installing.h"
#include "products/skyblip_go/pages/no_tx.h"
#include "products/skyblip_go/self_test_parts.h"
#include "products/skyblip_go/services/screen.h"
#include "protocols/common/geometry.h"

namespace skyblip::go {

namespace {
PpsState pps_state(const timing::ClockState& clock) {
    if (clock.pps_locked) return PpsState::Lock;
    return timing::in_pps_holdover(clock) ? PpsState::Holdover : PpsState::None;
}

std::optional<uint8_t> battery_percent(const power::BatteryState& battery) {
    return battery.valid ? std::optional<uint8_t>(battery.percent) : std::nullopt;
}

int traffic_outside_formation(const traffic::TrafficTable& table) {
    int count = 0;
    for (int i = 0; i < traffic::TrafficTable::kMaxTargets; i++) {
        const traffic::Target* target = table.at(i);
        if (target && target->used && !target->in_formation) count++;
    }
    return count;
}
}  // namespace

void ScreenService::draw_prompt(uint32_t now_ms) {
    ConfirmSnapshot snapshot;
    snapshot.title = common::pending_title(prompt_);
    snapshot.detail = config_.prompt_detail();
    snapshot.trusts_browser = config_.prompt_trusts_browser();
    snapshot.seconds_left = config_.prompt_seconds_left(now_ms);
    draw_confirm(framebuffer_, snapshot);
}

void ScreenService::draw_menu_page(uint32_t now_ms) {
    if (editor_.clearing()) {
        ClearSnapshot clear;
        clear.title = menu_row_label(editor_.field());
        clear.text = editor_.text();
        clear.seconds_left = editor_.clear_seconds_left(now_ms);
        draw_clear(framebuffer_, clear);
        return;
    }
    if (editor_.editing()) {
        CallsignSnapshot field;
        field.title = menu_row_label(editor_.field());
        field.text = editor_.text();
        field.cursor = editor_.cursor();
        field.clears = callsign_press_clears(field.text, field.cursor);
        if (editor_.field() == MenuRow::AircraftAddress) {
            field.own_transponder = comms::address_of(context_.state.own_transponder);
            field.refused = editor_.refused();
        }
        draw_callsign(framebuffer_, field);
        return;
    }
    MenuSnapshot snapshot;
    snapshot.page = editor_.page();
    snapshot.values = menu_values();
    snapshot.focus = editor_.focus();
    snapshot.aircraft = editor_.aircraft();
    detected_on_glass_ = snapshot.values.detected;
    draw_menu(framebuffer_, snapshot);
}

// INFO: fc 18oct26 the unlock swaps the activation page for the radar, through a wipe like any page
bool ScreenService::unit_activated() {
    const bool activated = context_.state.activation.activated();
    if (activated_ && *activated_ != activated) change_screen();
    activated_ = activated;
    return activated;
}

void ScreenService::present_self_test() {
    draw_self_test(framebuffer_, self_test_);
    context_.roles.display.present(framebuffer_, ports::Refresh::Full, 0);
}

void ScreenService::render(uint32_t now_ms) {
    if (answering()) {
        draw_prompt(now_ms);
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
    if (notice_) {
        switch (notice_kind_) {
            case Notice::BravoForgot: draw_bravo_forgot_notice(framebuffer_); break;
            case Notice::BravoAsks:
                draw_bravo_asks_notice(framebuffer_, seconds_left(bravo_.step_since_ms(), now_ms,
                                                                  BravoService::kAnswerCeilingMs));
                break;
            case Notice::NewAircraft:
                draw_aircraft_notice(framebuffer_, noticed_aircraft_,
                                     seconds_left(notice_since_ms_, now_ms, notice_ceiling_ms()));
                break;
        }
        return;
    }
    if (mode_ == Mode::Menu) {
        draw_menu_page(now_ms);
        return;
    }

    framebuffer_.clear(/*white=*/true);

    const model::Ownship& own = context_.state.own;
    const std::optional<model::Ownship> plotted = plotted_own();
    const go::Settings& settings = settings_;

    switch (page_) {
        case Page::Radar: {
            RadarSnapshot snapshot;
            snapshot.fix_valid = plotted.has_value();
            snapshot.bravo_fix = plotted && !own.fix_valid;
            snapshot.stage = context_.state.gnss.stage;
            snapshot.units = settings.units;
            snapshot.range_step = settings.range_step;
            snapshot.plot = settings.plot;
            snapshot.heard_count = traffic_outside_formation(context_.state.traffic);
            snapshot.track_cdeg = plotted.value_or(own).track_cdeg;
            snapshot.speed_mm_s = plotted.value_or(own).speed_mm_s;
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
            snapshot.off_in_min = off_in_min();
            snapshot.bravo = context_.state.bravo;
            snapshot.bravo_heard = bravo_.health() != nullptr;
            if (const comms::PeerHealth* health = bravo_health_live()) {
                snapshot.bravo_battery_low = power::needs_charge(health->power_level);
                snapshot.bravo_battery_percent = health->battery_percent;
            }
            int count = 0;
            if (plotted) {
                const model::Ownship own_now = flight::carried_to(*plotted, now_ms);
                traffic::RangeRow ranked[kMaxRadarTargets];
                const int ranked_count =
                    traffic::rank_by_relevance(context_.state.traffic, context_.state.callsigns,
                                               *plotted, ranked, kMaxRadarTargets);
                for (int i = 0; i < ranked_count; i++) {
                    const traffic::Target* target = context_.state.traffic.at(ranked[i].slot);
                    const model::AircraftObservation observation =
                        flight::carried_to(target->observation, now_ms);
                    int32_t north = 0, east = 0, up = 0;
                    bool up_valid = false;
                    if (!flight::relative_north_east_up(own_now, observation, north, east, up,
                                                        up_valid))
                        continue;
                    targets_[count].north_m = north;
                    targets_[count].east_m = east;
                    targets_[count].up_m = up;
                    targets_[count].up_valid = up_valid;
                    targets_[count].alarm_level = target->alarm_level;
                    targets_[count].alarm_dismissed = target->alarm_dismissed;
                    targets_[count].climb_e8 = observation.climb_e8;
                    targets_[count].climb_valid =
                        observation.climb_valid &&
                        (!observation.stealth || target->alarm_level != traffic::Level::None);
                    targets_[count].speed_mm_s =
                        observation.speed_valid
                            ? to_mm_s(QuarterMetresPerSecond(observation.speed_q)).value
                            : 0;
                    targets_[count].track_cdeg =
                        to_centi_degrees(Cordic9(observation.track_c9)).value;
                    targets_[count].turn_cdps = static_cast<int16_t>(target->turn.rate_dps * 100);
                    targets_[count].turn_valid = target->turn.valid;
                    targets_[count].in_formation = target->in_formation;
                    count++;
                }
            }
            snapshot.target_count = count;
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
            snapshot.solution_phase_ms = context_.state.gnss.solution_phase_ms;
            snapshot.solution_phase_valid = context_.state.gnss.solution_phase_valid;
            snapshot.health = context_.roles.gnss.health();
            snapshot.sky = &context_.state.gnss.sky;
            draw_satellites(framebuffer_, snapshot);
            break;
        }
        case Page::Nearby: {
            NearbySnapshot snapshot;
            snapshot.go = nearby_go();
            snapshot.bravo = nearby_bravo();
            snapshot.fix_valid = plotted.has_value();
            snapshot.bravo_fix = plotted && !own.fix_valid;
            snapshot.units = settings.units;
            snapshot.row_count =
                traffic::rank_by_relevance(context_.state.traffic, context_.state.callsigns,
                                           plotted.value_or(own), nearby_rows_, kNearbyRows);
            snapshot.rows = nearby_rows_;
            draw_nearby(framebuffer_, snapshot);
            break;
        }
        case Page::SelfTest: {
            // INFO: fc 18oct26 the parts are boot's, the address now's: an unlock needs no reboot
            SelfTestSnapshot now = self_test_;
            now.parts = self_test_now();
            now.address = context_.state.activation.address();
            now.aircraft_address_warning =
                bravo_.aircraft_address_warning().stands(settings.aircraft_address);
            now.bravo = context_.state.bravo;
            now.bravo_tag = events::bluetooth_tag(settings.bravo);
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
        case Page::BravoLog: {
            BravoLogSnapshot snapshot;
            snapshot.state = context_.state.bravo;
            snapshot.tag = events::bluetooth_tag(settings.bravo);
            snapshot.health = bravo_.health();
            snapshot.folded = bravo_.folded();
            snapshot.refused = bravo_.refused();
            snapshot.unplaced = bravo_.unplaced();
            snapshot.tracked = context_.state.traffic.count_of(model::Source::Adsb);
            snapshot.log = &bravo_.log();
            draw_bravo_log(framebuffer_, snapshot);
            break;
        }
        case Page::Bravo: {
            BravoSnapshot snapshot;
            snapshot.view = bravo_view();
            snapshot.focus = bravo_page_.focus();
            snapshot.confirming = bravo_page_.confirming();
            snapshot.unpairing = bravo_page_.unpairing();
            snapshot.seconds_left = bravo_seconds_left(snapshot.view, now_ms);
            draw_bravo(framebuffer_, snapshot);
            break;
        }
        case Page::Status:
        default: {
            StatusSnapshot snapshot;
            snapshot.address =
                settings::on_air(context_.state.activation.address(), settings.aircraft_address)
                    .address;
            snapshot.callsign = settings.callsign;
            snapshot.fix_valid = own.fix_valid;
            snapshot.utc_valid = own.utc_valid;
            snapshot.transmitting = context_.state.transmit.on_air();
            snapshot.no_tx = context_.state.transmit.unmet();
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
            snapshot.imu_meta_event = context_.state.imu.meta_event;
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
    draw_no_tx(framebuffer_, context_.state.transmit.shown());
}

// INFO: fc 19nov26 the parts are boot's, each with how it works now and whether it stops the burst
const SelfTestPart* ScreenService::self_test_now() {
    const timing::TransmitGate& gate = context_.state.transmit;
    const timing::NoTx shown = gate.shown();
    const int rows = std::min(self_test_.part_count, kSelfTestPartCount);
    for (int i = 0; i < rows; i++) {
        SelfTestPart& row = self_test_rows_[i];
        row = self_test_.parts[i];
        if (kSelfTestParts[i].capability == ports::Capability::Rf) {
            row.now = radio_word(gate.met());
            row.no_tx = shown == timing::NoTx::Radio;
        } else if (kSelfTestParts[i].capability == ports::Capability::Gnss) {
            row.now = receiver_word(gate.met(), context_.roles.gnss.health());
            row.no_tx = shown == timing::NoTx::Gnss || shown == timing::NoTx::Pps;
        }
    }
    return self_test_rows_;
}

NearbyGo ScreenService::nearby_go() const {
    const model::Ownship& own = context_.state.own;
    const traffic::TrafficTable& table = context_.state.traffic;
    NearbyGo go;
    go.battery_percent = battery_percent(context_.state.power.battery);
    go.charging = context_.state.power.battery.charging;
    go.stage = context_.state.gnss.stage;
    go.satellites = own.satellites;
    go.transmitting = context_.state.transmit.on_air();
    go.traffic = table.count() - table.count_of(model::Source::Adsb);
    return go;
}

NearbyBravo ScreenService::nearby_bravo() const {
    NearbyBravo bravo;
    bravo.state = context_.state.bravo;
    bravo.traffic = context_.state.traffic.count_of(model::Source::Adsb);
    const comms::PeerHealth* health = bravo_.health();
    bravo.heard = health != nullptr;
    if (health != nullptr) {
        bravo.battery_percent = health->battery_percent;
        bravo.charging = health->charging;
        bravo.stage = health->gnss_stage;
        bravo.satellites = health->satellites;
    }
    return bravo;
}

}  // namespace skyblip::go
