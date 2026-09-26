#ifndef SKYBLIP_CORE_DIAG_PAYLOAD_H
#define SKYBLIP_CORE_DIAG_PAYLOAD_H

#include <cstdint>

#include "core/dfu/update.h"
#include "core/diag/record.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/flight/state.h"
#include "core/gnss/acquisition.h"
#include "core/gnss/validity.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/radio/log.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot.h"
#include "core/traffic/alarm.h"

namespace skyblip::diag {

struct Boot {
    uint32_t capabilities{0};
    uint32_t fw_build{0};
    uint16_t fw_revision{0};
    uint8_t fw_major{0};
    uint8_t fw_minor{0};
    power::ResetReason reset{power::ResetReason::Unknown};
    dfu::ImageState image_state{dfu::ImageState::Confirmed};
};

constexpr uint8_t kConfigFlagAlarmEnabled = 1u << 2;
constexpr uint8_t kConfigFlagMetric = 1u << 3;
constexpr uint8_t kConfigFlagBatteryTrimManual = 1u << 4;

struct Config {
    uint32_t addr{0};
    int16_t battery_offset_mv{0};
    int16_t freq_trim_e1_ppm{0};
    uint8_t aircraft_type{0};
    uint8_t addr_table{0};
    uint8_t alarm_volume{0};
    uint8_t settings_version{0};
    bool alarm_enabled{false};
    bool metric{false};
    bool battery_trim_manual{false};
};

constexpr uint8_t kGnssFlagFixValid = 1u << 2;
constexpr uint8_t kGnssFlagResidValid = 1u << 3;
constexpr uint8_t kGnssFlagPpsLocked = 1u << 4;
constexpr uint8_t kGnssFlagGeoidMeasured = 1u << 5;
constexpr uint8_t kGnssFlagTxSettled = 1u << 6;

struct Gnss {
    uint16_t nav_ms{0};
    uint16_t resid_m{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    uint32_t stage_s{0};
    uint8_t sats{0};
    uint8_t sats_in_view{0};
    uint8_t fix_mode{0};
    gnss::FixReject reject{gnss::FixReject::None};
    gnss::Stage stage{gnss::Stage::Silent};
    bool fix_valid{false};
    bool resid_valid{false};
    bool pps_locked{false};
    bool geoid_measured{false};
    bool tx_settled{false};
};

constexpr uint8_t kPpsFlagLocked = 1u << 2;
constexpr uint8_t kPpsFlagUtcValid = 1u << 3;

struct Pps {
    uint32_t interval_us{0};
    int32_t error_us{0};
    uint32_t samples{0};
    uint32_t holdover_events{0};
    uint16_t since_edge_ms{0};
    bool locked{false};
    bool utc_valid{false};
};

constexpr uint8_t kBurstFlagAddrValid = 1u << 2;
constexpr uint8_t kBurstFlagRssiValid = 1u << 3;
constexpr uint8_t kBurstFlagAirborne = 1u << 4;
constexpr uint8_t kBurstFlagTxSpanValid = 1u << 5;
constexpr uint8_t kBurstFlagCallsign = 1u << 6;

enum class Refusal : uint8_t {
    None = 0,
    OverBudget = 1,
    Unarmed = 2,
    Unsettled = 3,
    OffSchedule = 4
};

constexpr uint8_t kDwellFlagTxAllowed = 1u << 2;
constexpr uint8_t kDwellFlagOwnTxDwell = 1u << 3;
constexpr uint8_t kDwellFlagListenOnly = 1u << 4;
constexpr uint8_t kDwellFlagArmed = 1u << 5;
constexpr uint8_t kDwellFlagBurstArmed = 1u << 6;

struct Dwell {
    uint32_t freq_hz{0};
    uint16_t start_ms{0};
    uint16_t end_ms{0};
    uint16_t phase_ms{0};
    uint16_t duty_permille{0};
    timing::SlotState state{timing::SlotState::UplinkRxO};
    model::Band band{model::Band::M};
    Refusal refusal{Refusal::None};
    int8_t noise_dbm{0};
    bool tx_allowed{false};
    bool own_tx_dwell{false};
    bool listen_only{false};
    bool armed{false};
    bool burst_armed{false};
};

constexpr uint8_t kFlightFlagFixValid = 1u << 2;
constexpr uint8_t kFlightFlagRolling = 1u << 3;
constexpr uint8_t kFlightFlagClimbValid = 1u << 4;
constexpr uint8_t kFlightFlagTxSettled = 1u << 5;

struct Flight {
    int32_t speed_mm_s{0};
    int32_t climb_mm_s{0};
    int16_t alt_msl_m{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    flight::FlightState declared{flight::FlightState::Unknown};
    flight::FlightState confirmed{flight::FlightState::Unknown};
    bool fix_valid{false};
    bool rolling{false};
    bool climb_valid{false};
    bool tx_settled{false};
};

constexpr uint8_t kPowerFlagCharging = 1u << 2;
constexpr uint8_t kPowerFlagExternal = 1u << 3;
constexpr uint8_t kPowerFlagValid = 1u << 4;
constexpr uint8_t kPowerFlagDieValid = 1u << 5;
constexpr uint8_t kPowerFlagCaution = 1u << 6;
constexpr uint8_t kPowerFlagTrimLearned = 1u << 7;

struct Power {
    uint16_t cell_mv{0};
    uint32_t supply_warnings{0};
    uint32_t implausible{0};
    uint32_t charge_warnings{0};
    int16_t die_dc{0};
    int16_t trim_offset_mv{0};
    uint8_t percent{0};
    power::PowerLevel level{power::PowerLevel::Unknown};
    power::ChargeCondition charge{power::ChargeCondition::Unknown};
    bool charging{false};
    bool external_power{false};
    bool valid{false};
    bool die_valid{false};
    bool caution{false};
    bool trim_learned{false};
};

constexpr uint8_t kBaroFlagActive = 1u << 2;
constexpr uint8_t kBaroFlagTemperatureValid = 1u << 3;
constexpr uint8_t kBaroFlagClimbAdopted = 1u << 4;

struct Baro {
    uint32_t pressure_mpa{0};
    int32_t alt_mm{0};
    int32_t climb_mm_s{0};
    int16_t temperature_dc{0};
    bool active{false};
    bool temperature_valid{false};
    bool climb_adopted{false};
};

constexpr uint8_t kMotionFlagSlipValid = 1u << 2;
constexpr uint8_t kMotionFlagGLoadValid = 1u << 3;
constexpr uint8_t kMotionFlagFitted = 1u << 4;

struct Motion {
    int16_t slip_mg{0};
    int16_t normal_mg{0};
    int16_t lateral_mg{0};
    int16_t longitudinal_mg{0};
    int16_t most_normal_mg{0};
    int16_t least_normal_mg{0};
    uint8_t imu_error{0};
    uint8_t sensor_error{0};
    bool slip_valid{false};
    bool gload_valid{false};
    bool fitted{false};
};

constexpr uint8_t kContactFlagDown = 1u << 2;

struct Contact {
    uint32_t at_ms{0};
    uint32_t held_ms{0};
    events::Contact contact{events::Contact::Button};
    uint8_t gesture{0};
    bool down{false};
};

enum class LinkAction : uint8_t {
    Up = 0,
    Down = 1,
    ClaimTaken = 2,
    ClaimReleased = 3,
    Received = 4,
    Sent = 5,
    Dropped = 6
};

constexpr uint8_t kLinkFlagClaimHeld = 1u << 2;

struct Link {
    uint16_t session{0};
    uint16_t payload_bytes{0};
    uint16_t frame_bytes{0};
    uint16_t holder{0};
    uint32_t drops{0};
    LinkAction action{LinkAction::Up};
    events::Endpoint endpoint{events::Endpoint::Config};
    bool claim_held{false};
};

constexpr uint8_t kTrafficFlagAssessed = 1u << 2;
constexpr uint8_t kTrafficFlagDismissed = 1u << 3;
constexpr uint8_t kTrafficFlagFormation = 1u << 4;
constexpr uint8_t kTrafficFlagPositionValid = 1u << 5;

struct Traffic {
    uint32_t addr{0};
    uint16_t dist_m{0};
    uint16_t bearing_deg{0};
    int16_t rel_alt_m{0};
    int16_t closing_mps{0};
    traffic::Level alarm{traffic::Level::None};
    model::Source source{model::Source::AdslDirect};
    int8_t rssi_dbm{0};
    uint8_t tracked{0};
    bool assessed{false};
    bool dismissed{false};
    bool in_formation{false};
    bool position_valid{false};
};

constexpr uint8_t kWriteFlagPending = 1u << 2;

struct Write {
    uint32_t waited_ms{0};
    uint16_t phase_ms{0};
    uint32_t requests{0};
    uint32_t writes{0};
    uint32_t forced{0};
    timing::DurableWriteVerdict placement{timing::DurableWriteVerdict::Idle};
    power::DurableWrite kind{power::DurableWrite::Settings};
    bool pending{false};
};

constexpr uint8_t kScreenFlagBacklight = 1u << 2;
constexpr uint8_t kScreenFlagPowered = 1u << 3;
constexpr uint8_t kScreenFlagHolding = 1u << 4;

struct Screen {
    uint32_t since_ms{0};
    uint8_t page{0};
    uint8_t mode{0};
    uint8_t prompt{0};
    traffic::Level alarm{traffic::Level::None};
    bool backlight{false};
    bool powered{false};
    bool holding{false};
};

struct Gap {
    uint32_t dropped{0};
    uint32_t span_ms{0};
    uint32_t total{0};
    uint16_t capacity{0};
};

// INFO: fc 20sep26 a diagnostics slot carries no CRC, so this is what says the tail is not torn
struct End {
    uint32_t records{0};
    uint32_t dropped{0};
};

// INFO: fc 21sep26 a counter crosses the wire as its low half, so 65.536 s of it is the ceiling
constexpr uint32_t kDutyMaxPeriodMs = 60000;

struct Duty {
    uint32_t panel_partial_refreshes{0};
    uint32_t panel_full_refreshes{0};
    uint32_t backlight_ms{0};
    uint32_t rx_armed_ms{0};
    uint32_t tx_keyed_ms{0};
    uint32_t ble_connected_ms{0};
    uint32_t annunciator_ms{0};
};

Record record_of(const Boot& value, const Instant& at);
Record record_of(const Config& value, const Instant& at);
Record record_of(const Gnss& value, const Instant& at);
Record record_of(const Pps& value, const Instant& at);
Record record_of(const radio::Entry& value);
Record record_of(const Dwell& value, const Instant& at);
Record record_of(const Flight& value, const Instant& at);
Record record_of(const Power& value, const Instant& at);
Record record_of(const Baro& value, const Instant& at);
Record record_of(const Motion& value, const Instant& at);
Record record_of(const Contact& value, const Instant& at);
Record record_of(const Link& value, const Instant& at);
Record record_of(const Traffic& value, const Instant& at);
Record record_of(const Write& value, const Instant& at);
Record record_of(const Screen& value, const Instant& at);
Record record_of(const Gap& value, const Instant& at);
Record record_of(const End& value, const Instant& at);
Record record_of(const Duty& value, const Instant& at);

bool read(const Record& record, Boot& out);
bool read(const Record& record, Config& out);
bool read(const Record& record, Gnss& out);
bool read(const Record& record, Pps& out);
bool read(const Record& record, radio::Entry& out);
bool read(const Record& record, Dwell& out);
bool read(const Record& record, Flight& out);
bool read(const Record& record, Power& out);
bool read(const Record& record, Baro& out);
bool read(const Record& record, Motion& out);
bool read(const Record& record, Contact& out);
bool read(const Record& record, Link& out);
bool read(const Record& record, Traffic& out);
bool read(const Record& record, Write& out);
bool read(const Record& record, Screen& out);
bool read(const Record& record, Gap& out);
bool read(const Record& record, End& out);
bool read(const Record& record, Duty& out);

}  // namespace skyblip::diag

#endif
