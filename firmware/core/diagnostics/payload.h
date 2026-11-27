#ifndef SKYBLIP_CORE_DIAGNOSTICS_PAYLOAD_H
#define SKYBLIP_CORE_DIAGNOSTICS_PAYLOAD_H

#include <cstdint>

#include "core/comms/peer.h"
#include "core/dfu/update.h"
#include "core/diagnostics/record.h"
#include "core/events/input.h"
#include "core/events/link.h"
#include "core/flight/state.h"
#include "core/gnss/acquisition.h"
#include "core/gnss/validity.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/model/ownship.h"
#include "core/power/charging.h"
#include "core/power/cutoff.h"
#include "core/power/reset_reason.h"
#include "core/radio/log.h"
#include "core/timing/durable_write.h"
#include "core/timing/slot.h"
#include "core/traffic/alarm.h"
#include "ports/adsb_receiver.h"

namespace skyblip::diagnostics {

struct Boot {
    uint32_t capabilities{0};
    uint32_t firmware_build{0};
    uint16_t firmware_revision{0};
    uint8_t firmware_major{0};
    uint8_t firmware_minor{0};
    power::ResetReason reset{power::ResetReason::Unknown};
    dfu::ImageState image_state{dfu::ImageState::Confirmed};
    Profile profile{Profile::Full};
    bool profile_recorded{false};
};

struct Config {
    uint32_t address{0};
    int16_t battery_offset_mv{0};
    int16_t frequency_trim_e1_ppm{0};
    uint8_t aircraft_type{0};
    uint8_t address_mapping_table{0};
    uint8_t settings_version{0};
    int8_t tx_power_dbm{0};
    int8_t pa_rated_dbm{0};
    uint8_t alarm_setting{0};
    bool alarm_setting_recorded{false};
    bool metric{false};
    bool battery_trim_manual{false};
};

struct Gnss {
    uint16_t solution_phase_ms{0};
    uint16_t residual_m{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    uint32_t stage_s{0};
    uint8_t satellites{0};
    uint8_t satellites_in_view{0};
    uint8_t fix_mode{0};
    gnss::FixReject reject{gnss::FixReject::None};
    gnss::Stage stage{gnss::Stage::Silent};
    bool fix_valid{false};
    bool residual_valid{false};
    bool pps_locked{false};
    bool geoid_measured{false};
    bool tx_settled{false};
    bool levels_fresh{false};
};

struct Pps {
    uint32_t interval_us{0};
    int32_t error_us{0};
    uint32_t samples{0};
    uint32_t holdover_events{0};
    uint16_t since_edge_ms{0};
    bool locked{false};
    bool utc_valid{false};
};

struct Dwell {
    uint32_t frequency_hz{0};
    uint16_t start_ms{0};
    uint16_t end_ms{0};
    uint16_t phase_ms{0};
    uint16_t duty_permille{0};
    timing::SlotState state{timing::SlotState::UplinkRxOBand};
    model::Band band{model::Band::M};
    Refusal refusal{Refusal::None};
    int8_t noise_dbm{0};
    bool tx_allowed{false};
    bool own_tx_dwell{false};
    bool listen_only{false};
    bool armed{false};
    bool burst_armed{false};
};

struct Flight {
    int32_t speed_mm_s{0};
    int32_t climb_mm_s{0};
    int16_t altitude_msl_m{0};
    uint16_t hdop_e2{0};
    uint16_t vdop_e2{0};
    flight::FlightState declared{flight::FlightState::Unknown};
    flight::FlightState confirmed{flight::FlightState::Unknown};
    bool fix_valid{false};
    bool rolling{false};
    bool climb_valid{false};
    bool tx_settled{false};
};

struct Power {
    uint16_t cell_mv{0};
    uint32_t supply_warnings{0};
    uint32_t implausible{0};
    uint32_t charge_warnings{0};
    int16_t die_dc{0};
    int8_t sample_offset_mv{0};
    uint8_t percent{0};
    power::PowerLevel power_level{power::PowerLevel::Unknown};
    power::ChargeCondition charge{power::ChargeCondition::Unknown};
    bool charging{false};
    bool external_power{false};
    bool battery_valid{false};
    bool die_valid{false};
};

struct Barometer {
    uint32_t pressure_mpa{0};
    int32_t altitude_mm{0};
    int32_t climb_mm_s{0};
    int16_t temperature_dc{0};
    bool active{false};
    bool temperature_valid{false};
    bool climb_adopted{false};
};

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

struct Contact {
    uint32_t at_ms{0};
    uint32_t held_ms{0};
    events::Contact contact{events::Contact::Button};
    uint8_t gesture{0};
    bool down{false};
};

struct Link {
    uint16_t link_session{0};
    uint16_t payload_bytes{0};
    uint16_t frame_bytes{0};
    uint16_t holder{0};
    uint32_t drops{0};
    LinkAction action{LinkAction::Up};
    events::Endpoint endpoint{events::Endpoint::Config};
    events::MtuExchangeStarter mtu_exchange_started_by{events::MtuExchangeStarter::None};
    bool claim_held{false};
};

struct Traffic {
    uint32_t address{0};
    uint16_t distance_m{0};
    uint16_t bearing_deg{0};
    int16_t relative_altitude_m{0};
    int16_t closing_mps{0};
    traffic::Level alarm{traffic::Level::None};
    model::Source source{model::Source::AdslDirect};
    int8_t rssi_dbm{0};
    uint8_t tracked{0};
    bool assessed{false};
    bool dismissed{false};
    bool in_formation{false};
    bool position_valid{false};
    bool relative_altitude_valid{false};
};

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

struct Screen {
    uint32_t since_page_shown_ms{0};
    uint8_t page{0};
    uint8_t mode{0};
    uint8_t prompt{0};
    traffic::Level alarm{traffic::Level::None};
    bool backlight{false};
    bool powered{false};
    bool holding{false};
    uint32_t render_us{0};
    uint32_t present_us{0};
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
constexpr uint32_t kDutyPeriodCeilingMs = 60000;

struct Duty {
    uint32_t panel_partial_refreshes{0};
    uint32_t panel_full_refreshes{0};
    uint32_t backlight_ms{0};
    uint32_t rx_armed_ms{0};
    uint32_t tx_keyed_ms{0};
    uint32_t ble_connected_ms{0};
    uint32_t annunciator_ms{0};
};

struct Switch {
    uint32_t to_hz{0};
    int32_t margin_us{0};
    uint16_t took_us{0};
    uint16_t gap_us{0};
    SwitchKind change{SwitchKind::Retune};
    bool armed_ahead{false};
    bool late{false};
};

struct Loop {
    uint16_t passes{0};
    uint16_t busy_ms{0};
    uint32_t worst_pass_us{0};
    uint16_t worst_pass_phase_ms{0};
    uint16_t worst_gap_ms{0};
    uint16_t worst_gap_phase_ms{0};
    LoopService worst_service{LoopService::Config};
    uint8_t worst_tick_ms{0};
};

struct Adsb {
    uint32_t address{0};
    int32_t pressure_altitude_ft{0};
    int32_t height_difference_ft{0};
    int32_t height_difference_age_ms{0};
    int32_t gnss_relative_altitude_m{0};
    int32_t pressure_relative_altitude_m{0};
    uint8_t adsb_version{0};
    uint8_t gva{0};
    bool adsb_version_valid{false};
    bool gva_valid{false};
    bool pressure_altitude_valid{false};
    bool height_difference_valid{false};
    bool gnss_relative_altitude_valid{false};
    bool pressure_relative_altitude_valid{false};
};

Adsb adsb_of(const model::Ownship& own, const model::AircraftObservation& target, uint32_t now_ms);

// INFO: fc 26nov26 every counter crosses as its low bits, which readers subtract, as Duty's do
struct Receiver {
    uint32_t frames{0};
    uint32_t part_packets{0};
    uint16_t part_errors{0};
    uint8_t gain_step{0};
    uint32_t rearms{0};
    uint32_t resets{0};
    uint32_t part_resets{0};
    uint32_t command_errors{0};
    uint32_t missed_edges{0};
    int8_t noise_dbm{0};
    int8_t threshold_dbm{0};
    ports::ReceiverMode part_mode{ports::ReceiverMode::Unknown};
    ports::ReceiverReset part_reset{ports::ReceiverReset::None};
    bool listening{false};
    bool irq_armed{false};
    bool probed{false};
    bool interrupt_pending{false};
    bool front_end_kept{false};
    bool boosted{false};
};

Receiver receiver_of(const ports::AdsbReceiverStatistics& statistics, bool listening);

struct Reception {
    uint32_t crc{0};
    uint32_t unsupported{0};
    uint32_t folded{0};
    uint32_t positions{0};
    uint32_t capture_refused{0};
    uint32_t aircraft{0};
    uint32_t tracked{0};
    uint32_t framing_resyncs{0};
    uint32_t impossible_formats{0};
    comms::ExportReference reference{comms::ExportReference::None};
    bool fix_valid{false};
};

struct Framing {
    uint32_t overflow_drops{0};
    uint32_t resync_drops{0};
    uint32_t queue_drops{0};
    uint32_t in_flight{0};
    uint32_t split_counts{0};
    uint32_t drain_busy_ms{0};
    uint32_t drains{0};
    uint32_t stranded_resyncs{0};
    uint32_t short_resyncs{0};
};

Framing framing_of(const ports::AdsbReceiverStatistics& statistics);

struct Screening {
    uint32_t squitters{0};
    uint32_t all_calls{0};
    uint32_t replies{0};
    uint32_t parity_refused{0};
    uint32_t one_bit_errors{0};
    uint32_t unmatched{0};
    uint32_t two_bit_errors{0};
    uint32_t fifo_peak_frames{0};
    uint32_t queue_peak_frames{0};
};

Screening screening_of(const ports::AdsbReceiverStatistics& statistics, uint32_t unmatched);

struct Drain {
    uint32_t irq_wakes{0};
    uint32_t empty_wakes{0};
    uint32_t poll_ms{0};
    uint32_t worst_drain_us{0};
    uint32_t busy_wait_ms{0};
    uint32_t transactions{0};
    uint32_t repaired{0};
    uint32_t repairs_refused{0};
};

Drain drain_of(const ports::AdsbReceiverStatistics& statistics, uint32_t repaired,
               uint32_t repairs_refused);

Record record_of(const Boot& value, const Instant& at);
Record record_of(const Config& value, const Instant& at);
Record record_of(const Gnss& value, const Instant& at);
Record record_of(const Pps& value, const Instant& at);
Record record_of(const radio::Entry& value);
Record record_of(const Dwell& value, const Instant& at);
Record record_of(const Flight& value, const Instant& at);
Record record_of(const Power& value, const Instant& at);
Record record_of(const Barometer& value, const Instant& at);
Record record_of(const Motion& value, const Instant& at);
Record record_of(const Contact& value, const Instant& at);
Record record_of(const Link& value, const Instant& at);
Record record_of(const Traffic& value, const Instant& at);
Record record_of(const Write& value, const Instant& at);
Record record_of(const Screen& value, const Instant& at);
Record record_of(const Gap& value, const Instant& at);
Record record_of(const End& value, const Instant& at);
Record record_of(const Duty& value, const Instant& at);
Record record_of(const Switch& value, const Instant& at);
Record record_of(const Loop& value, const Instant& at);
Record record_of(const Adsb& value, const Instant& at);
Record record_of(const Receiver& value, const Instant& at);
Record record_of(const Reception& value, const Instant& at);
Record record_of(const Framing& value, const Instant& at);
Record record_of(const Screening& value, const Instant& at);
Record record_of(const Drain& value, const Instant& at);

}  // namespace skyblip::diagnostics

#endif
