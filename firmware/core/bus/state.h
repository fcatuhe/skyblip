#ifndef SKYBLIP_CORE_BUS_STATE_H
#define SKYBLIP_CORE_BUS_STATE_H

#include "core/events/rf.h"
#include "core/flight/atmosphere.h"
#include "core/flight/gload.h"
#include "core/flight/ground.h"
#include "core/gnss/acquisition.h"
#include "core/gnss/sky.h"
#include "core/gnss/validity.h"
#include "core/model/ownship.h"
#include "core/power/battery.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/radio/log.h"
#include "core/timing/channel.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot.h"
#include "core/timing/timing_stats.h"
#include "core/traffic/callsigns.h"
#include "core/traffic/table.h"

namespace skyblip::bus {

// Where the radio believes it is inside the second it is arming, stamped with
// the pass it said so on. The radio service is the only writer; whoever needs
// to know whether the core may be stalled reads it here rather than deriving
// the phase a second time (core/timing/durable_write.h).
struct RfState {
    timing::SlotPlan plan{};
    timing::DwellPhase dwell{};
    // The bench accumulator G6 reads out: boards/ is the one writer of
    // the PPS half, products/skyblip_go/services/radio.cpp of the dwell half.
    timing::SlotTimingStats timing_stats{};
    uint64_t tx_deadline_us{0};
    // INFO: fc 20sep26 which payload the armed burst carries, for the row the tape writes
    bool tx_callsign{false};
    uint32_t duty_permille{0};
    uint16_t last_tx_keyed_us{0};
    uint16_t last_tx_span_us{0};
    int8_t noise_dbm{timing::NoiseFloor::kSeedDbm};
};

struct PowerState {
    power::BatteryState battery{};
    // What the cutoff monitor made of the same samples the gauge saw. Whoever
    // draws a low cell reads this rather than comparing millivolts again: the
    // debounce, the charger and the sanity floor are decided once.
    power::PowerLevel level{power::PowerLevel::Unknown};
    power::ChargeCondition charge{power::ChargeCondition::Unknown};
    bool caution{false};
    bool supply_warned{false};
    int16_t die_dc{0};
    bool die_valid{false};
};

struct FlightStatus {
    uint32_t gnss_solutions{0};
    uint32_t seconds{0};
    bool time_valid{false};
    bool running{false};
    bool rolling{false};
    flight::FlightState confirmed_state{flight::FlightState::Unknown};
};

struct GnssStatus {
    gnss::Stage stage{gnss::Stage::Silent};
    uint32_t stage_s{0};
    uint8_t fix_mode{0};
    // INFO: fc 19sep26 how far into its own second a solution landed, what kFixLagMaxMs bounds
    uint16_t solution_phase_ms{0};
    bool solution_phase_valid{false};
    bool levels_wanted{false};
    // INFO: fc 18sep26 false once GSV is switched off, so no page draws a level nobody measured
    bool levels_live{false};
    gnss::FixReject reject{gnss::FixReject::None};
    uint32_t rejected{0};
    gnss::SkyView sky{};
};

struct BaroState {
    uint32_t pressure_mpa{0};
    int16_t temperature_decicelsius{0};
    bool temperature_valid{false};
    bool active{false};
};

struct FormationState {
    int members{0};
};

struct SlipState {
    int16_t lateral_mg{0};
    bool valid{false};
};

struct GLoadState {
    flight::GLoad now{};
    flight::GLoad most{};
    flight::GLoad least{};
    bool valid{false};
};

struct ImuState {
    const char* stage{"NONE"};
    const char* fault{""};
    uint32_t fifo_bytes{0};
    uint32_t unparsed{0};
    uint8_t error{0};
    uint8_t interrupt{0};
    uint8_t meta{0};
    uint8_t sensor_error{0};
    uint8_t errored_sensor{0};
};

struct DutyState {
    uint32_t panel_partial_refreshes{0};
    uint32_t panel_full_refreshes{0};
    uint32_t backlight_ms{0};
    uint32_t rx_armed_ms{0};
    uint32_t tx_keyed_ms{0};
    uint32_t annunciator_ms{0};
    uint32_t ble_connected_ms{0};
};

enum class CaptureStop : uint8_t { None, Pilot, NoSectors, NoStorage };

struct CaptureState {
    uint32_t session_id{0};
    uint32_t records{0};
    uint32_t dropped{0};
    uint32_t sectors{0};
    uint32_t pool_sectors{0};
    uint32_t price_sectors{0};
    uint32_t price_flights{0};
    // INFO: fc 20sep26 the ring rotates: what the partition keeps of a capture, not a deadline
    uint32_t keeps_s{0};
    uint32_t faults{0};
    uint32_t unreadable_sectors{0};
    CaptureStop stopped{CaptureStop::None};
    bool armed{false};
    bool available{false};
};

struct State {
    model::OwnState own{};
    timing::ClockState clock{};
    traffic::TrafficTable traffic{};
    traffic::CallsignTable callsigns{};
    radio::Log radio_log{};
    RfState rf{};
    PowerState power{};
    FlightStatus flight{};
    GnssStatus gnss{};
    BaroState baro{};
    SlipState slip{};
    GLoadState gload{};
    ImuState imu{};
    FormationState formation{};
    CaptureState capture{};
    DutyState duty{};

    traffic::Level alarm_level{traffic::Level::None};
    traffic::Level alarm_live{traffic::Level::None};

    struct AirCounts {
        uint32_t rx_ok{0};
        uint32_t rx_bad{0};
        uint32_t rx_wait{0};
        uint32_t rx_type{0};
        uint32_t rx_unframed{0};
        uint32_t rx_miskeyed{0};
        uint32_t rx_noise{0};
        uint32_t rx_named{0};
        uint32_t tx_ok{0};
        uint32_t tx_lost{0};
        uint32_t tx_named{0};
        // INFO: fc 05aug26 The O-band uplink is its own path and is counted apart
        // from the M band's: every frame that arrived in the uplink dwell, the ones
        // Reed-Solomon refused, and the aircraft the rest of them put in the table.
        // The third is smaller than the aircraft the frames carried whenever the
        // ground station relayed one back that the table refuses - own-ship, or an
        // aircraft we are hearing better first-hand. Until 2026-08-05 an uplink
        // frame reached protocol::receive_mband, failed to frame as either M-band
        // system and landed in rx_bad: the whole feature was absent and its absence
        // looked like radio noise, which is what hid it.
        uint32_t uplink_frames{0};
        uint32_t uplink_bad{0};
        uint32_t uplink_targets{0};
        // The instant the executor actually reported completion for, published by
        // whoever already drains events::RfEvent (TrafficService) so the policy
        // layer that owns the deadline (RadioService) can measure against it
        // without a second reader of the bus.
        uint64_t last_tx_done_at_us{0};
    } air{};

    bool panel_presented{false};
    bool started{false};

    // The traffic table's single time base. Mixing GNSS epoch seconds with
    // boot-relative seconds underflows uint32 and ages every target out at once.
    uint32_t traffic_now(uint32_t now_ms) const {
        if (clock.pps_locked && clock.utc_s != 0) return clock.utc_s;
        return own.utc_valid ? own.utc : now_ms / 1000;
    }
};

}  // namespace skyblip::bus

#endif
