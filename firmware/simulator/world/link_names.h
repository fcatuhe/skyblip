#ifndef SKYBLIP_SIMULATOR_WORLD_LINK_NAMES_H
#define SKYBLIP_SIMULATOR_WORLD_LINK_NAMES_H

#include <cstdint>

#include "core/events/link.h"

namespace skyblip::simulator::link_names {

enum class Type : uint8_t { Unsigned, Integer, Boolean, Text, Bytes, UnsignedArray, Enum };

enum class Sender : uint8_t { Host, Unit, Both };

struct Value {
    const char* name;
    uint32_t code;
};

struct Enumeration {
    const Value* values;
    int count;
};

struct Key {
    const char* name;
    uint32_t number;
    Type type;
    const Enumeration* enumeration;
    int bytes;
    bool required;
};

struct Frame {
    const char* command;
    uint32_t code;
    events::Endpoint endpoint;
    Sender sender;
    const Key* keys;
    int count;
};

inline constexpr Value kReasonValues[] = {
    {"confirm", 0},
    {"confirm_dfu", 1},
    {"confirm_recovery", 2},
    {"confirm_power_off", 3},
    {"confirm_gnss_cold_start", 4},
    {"confirm_erase_log", 5},
    {"confirm_register", 6},
    {"cancelled", 7},
    {"expired", 8},
    {"in_flight", 9},
    {"low_power", 10},
    {"claimed", 11},
    {"no_command", 12},
    {"unknown_command", 13},
    {"no_version", 14},
    {"older", 15},
    {"nothing_staged", 16},
    {"not_approved", 17},
    {"wrong_key", 18},
    {"no_statistics", 19},
    {"install", 20},
    {"dfu", 21},
    {"recovery", 22},
    {"power_off", 23},
    {"erase_log", 24},
    {"gnss_cold_start", 25},
    {"unlock", 26},
    {"keyless", 27},
    {"no_session", 28},
    {"bad_signature", 29},
    {"small_mtu", 30},
    {"flash", 31},
    {"invalid", 32},
    {"out_of_range", 33},
    {"unknown_log", 34},
    {"no_diagnostics", 35},
    {"no_storage", 36},
    {"busy", 37},
    {"flights_only", 38},
    {"read_failed", 40},
    {"erased", 41},
    {"prompt_pending", 42},
    {"refused_on_device", 43},
    {"confirm_capture", 44},
    {"capture", 45},
    {"capturing", 46},
    {"not_capturing", 47},
    {"trusted", 48},
    {"untrusted", 49},
    {"forgotten", 50},
    {"unknown_browser", 51},
    {"not_own_transponder", 52},
};
inline constexpr Enumeration kReason{kReasonValues, 52};

inline constexpr Value kAlarmVolumeValues[] = {
    {"off", 0},
    {"soft", 1},
    {"loud", 2},
};
inline constexpr Enumeration kAlarmVolume{kAlarmVolumeValues, 3};

inline constexpr Value kUnitsValues[] = {
    {"nautical", 0},
    {"metric", 1},
};
inline constexpr Enumeration kUnits{kUnitsValues, 2};

inline constexpr Value kFlightValues[] = {
    {"unknown", 0},
    {"ground", 1},
    {"airborne", 2},
};
inline constexpr Enumeration kFlight{kFlightValues, 3};

inline constexpr Value kPowerLevelValues[] = {
    {"unknown", 0}, {"normal", 1}, {"critical", 2}, {"flat", 3}, {"low", 4},
};
inline constexpr Enumeration kPowerLevel{kPowerLevelValues, 5};

inline constexpr Value kGroupValues[] = {
    {"system", 0}, {"radio", 1}, {"traffic", 2}, {"gnss", 3},
    {"power", 4},  {"adsb", 5},  {"capture", 6},
};
inline constexpr Enumeration kGroup{kGroupValues, 7};

inline constexpr Value kResetValues[] = {
    {"unknown", 0},  {"power_on", 1}, {"pin", 2},          {"brownout", 3},       {"software", 4},
    {"watchdog", 5}, {"lockup", 6},   {"charger_wake", 7}, {"low_power_wake", 8}, {"debug", 9},
};
inline constexpr Enumeration kReset{kResetValues, 10};

inline constexpr Value kGnssConfigValues[] = {
    {"idle", 0},      {"restarting", 1}, {"waking", 2}, {"identifying", 3}, {"sending", 4},
    {"verifying", 5}, {"confirming", 6}, {"ready", 7},  {"degraded", 8},
};
inline constexpr Enumeration kGnssConfig{kGnssConfigValues, 9};

inline constexpr Value kRejectValues[] = {
    {"none", 0},  {"no_solution", 1}, {"missing_rmc", 2}, {"missing_gga", 3},
    {"stale", 4}, {"no_date", 5},     {"jump", 6},
};
inline constexpr Enumeration kReject{kRejectValues, 7};

inline constexpr Value kChargeValues[] = {
    {"unknown", 0},
    {"ok", 1},
    {"too_cold", 2},
    {"too_hot", 3},
};
inline constexpr Enumeration kCharge{kChargeValues, 4};

inline constexpr Value kImageValues[] = {
    {"confirmed", 0},
    {"probation", 1},
    {"reverted", 2},
    {"refused", 3},
};
inline constexpr Enumeration kImage{kImageValues, 4};

inline constexpr Value kSettingsFallbackValues[] = {
    {"none", 0},
    {"prior", 1},
    {"defaults", 2},
};
inline constexpr Enumeration kSettingsFallback{kSettingsFallbackValues, 3};

inline constexpr Value kHubImageValues[] = {
    {"none", 0}, {"missing", 1}, {"corrupt", 2}, {"unreadable", 3}, {"writing", 4}, {"held", 5},
};
inline constexpr Enumeration kHubImage{kHubImageValues, 6};

inline constexpr Value kExportReferenceValues[] = {
    {"none", 0},
    {"go", 1},
    {"own", 2},
};
inline constexpr Enumeration kExportReference{kExportReferenceValues, 3};

inline constexpr Value kBravoValues[] = {
    {"waiting", 1}, {"live", 2}, {"link_down", 3}, {"receiver_down", 4}, {"no_utc", 5},
};
inline constexpr Enumeration kBravo{kBravoValues, 5};

inline constexpr Value kClimbSourceValues[] = {
    {"unknown", 0},
    {"gnss", 1},
    {"barometric", 2},
};
inline constexpr Enumeration kClimbSource{kClimbSourceValues, 3};

inline constexpr Value kProductValues[] = {
    {"skyblip_go", 0},
    {"skyblip_bravo", 1},
};
inline constexpr Enumeration kProduct{kProductValues, 2};

inline constexpr Value kLogValues[] = {
    {"flights", 1},
    {"diagnostics", 2},
};
inline constexpr Enumeration kLog{kLogValues, 2};

inline constexpr Value kCaptureProfileValues[] = {
    {"full", 0},
    {"power_run", 1},
    {"flight_run", 2},
    {"stop", 3},
};
inline constexpr Enumeration kCaptureProfile{kCaptureProfileValues, 4};

inline constexpr Value kStagedCheckValues[] = {
    {"unchecked", 0}, {"matched", 1}, {"damaged", 2}, {"unreadable", 3}, {"undigested", 4},
};
inline constexpr Enumeration kStagedCheck{kStagedCheckValues, 5};

inline constexpr Value kSwapTypeValues[] = {
    {"unknown", 0}, {"none", 1}, {"test", 2},  {"permanent", 3},
    {"revert", 4},  {"fail", 5}, {"panic", 6},
};
inline constexpr Enumeration kSwapType{kSwapTypeValues, 7};

inline constexpr Value kProofValues[] = {
    {"none", 0},         {"booted", 1}, {"started", 2},        {"gnss_heard", 3},
    {"hub_bootable", 4}, {"proven", 5}, {"confirm_failed", 6},
};
inline constexpr Enumeration kProof{kProofValues, 7};

inline constexpr Value kCommandValues[] = {
    {"acknowledgement", 0},
    {"get", 1},
    {"config", 2},
    {"defaults", 3},
    {"set", 4},
    {"status", 5},
    {"timing", 6},
    {"flash", 7},
    {"radio", 8},
    {"diagnostics", 9},
    {"update", 10},
    {"dfu", 11},
    {"recovery", 12},
    {"power_off", 13},
    {"challenge", 14},
    {"register", 15},
    {"registration", 16},
    {"unlock", 17},
    {"list", 18},
    {"read", 19},
    {"erase", 20},
    {"log", 21},
    {"session", 22},
    {"chunk", 23},
    {"gnss_cold_start", 24},
    {"ownship", 25},
    {"observation", 26},
    {"health", 27},
    {"peer_claim", 28},
    {"capture", 29},
    {"browser_challenge", 30},
    {"trust_browser", 31},
    {"browsers", 32},
    {"browser", 33},
    {"forget_browser", 34},
    {"install_report", 35},
    {"protocols", 36},
};
inline constexpr Enumeration kCommand{kCommandValues, 37};

inline constexpr Key kAcknowledgementKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"acknowledge", 1, Type::Boolean, nullptr, 0, true},
    {"pending", 2, Type::Boolean, nullptr, 0, false},
    {"reason", 3, Type::Enum, &kReason, 0, false},
    {"by", 4, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kGetKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
};

inline constexpr Key kConfigKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"version", 1, Type::Unsigned, nullptr, 0, false},
    {"address", 2, Type::Unsigned, nullptr, 0, false},
    {"address_mapping_table", 3, Type::Unsigned, nullptr, 0, false},
    {"aircraft_type", 4, Type::Unsigned, nullptr, 0, false},
    {"alarm_enabled", 5, Type::Boolean, nullptr, 0, false},
    {"alarm_volume", 6, Type::Enum, &kAlarmVolume, 0, false},
    {"units", 7, Type::Enum, &kUnits, 0, false},
    {"callsign", 8, Type::Text, nullptr, 0, false},
    {"battery_offset_mv", 9, Type::Integer, nullptr, 0, false},
    {"frequency_trim_e1_ppm", 10, Type::Integer, nullptr, 0, false},
    {"own_icao", 11, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kStatusKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"flight", 1, Type::Enum, &kFlight, 0, true},
    {"upload", 2, Type::Boolean, nullptr, 0, true},
    {"battery_percent", 3, Type::Unsigned, nullptr, 0, false},
    {"charging", 4, Type::Boolean, nullptr, 0, true},
    {"power_level", 5, Type::Enum, &kPowerLevel, 0, true},
    {"went_dark_flat", 6, Type::Boolean, nullptr, 0, true},
    {"die_temperature_c", 7, Type::Integer, nullptr, 0, false},
    {"bravo", 8, Type::Enum, &kBravo, 0, false},
};

inline constexpr Key kTimingKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"part", 1, Type::Unsigned, nullptr, 0, true},
    {"more", 2, Type::Boolean, nullptr, 0, true},
    {"pps_us", 3, Type::UnsignedArray, nullptr, 0, false},
    {"pps_worst_us", 4, Type::Integer, nullptr, 0, false},
    {"pps_samples", 5, Type::Unsigned, nullptr, 0, false},
    {"dwell_us", 6, Type::UnsignedArray, nullptr, 0, false},
    {"dwell_worst_us", 7, Type::Integer, nullptr, 0, false},
    {"dwell_samples", 8, Type::Unsigned, nullptr, 0, false},
    {"holdover_events", 9, Type::Unsigned, nullptr, 0, false},
    {"missed", 10, Type::Unsigned, nullptr, 0, false},
    {"refused", 11, Type::Unsigned, nullptr, 0, false},
    {"switch_hop_us", 12, Type::Unsigned, nullptr, 0, false},
    {"switch_to_o_band_us", 13, Type::Unsigned, nullptr, 0, false},
    {"switch_to_m_band_us", 14, Type::Unsigned, nullptr, 0, false},
    {"late_dwells", 15, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kFlashKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"writable", 1, Type::Boolean, nullptr, 0, true},
    {"requests", 2, Type::Unsigned, nullptr, 0, true},
    {"writes", 3, Type::Unsigned, nullptr, 0, true},
    {"forced", 4, Type::Unsigned, nullptr, 0, true},
    {"worst_wait_ms", 5, Type::Unsigned, nullptr, 0, true},
    {"pending", 6, Type::Boolean, nullptr, 0, true},
    {"budget_ms", 7, Type::Unsigned, nullptr, 0, true},
    {"bound_ms", 8, Type::Unsigned, nullptr, 0, true},
};

inline constexpr Key kDiagnosticsKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"group", 1, Type::Enum, &kGroup, 0, true},
    {"part", 2, Type::Unsigned, nullptr, 0, true},
    {"more", 3, Type::Boolean, nullptr, 0, true},
    {"uptime_s", 4, Type::Unsigned, nullptr, 0, false},
    {"reset", 5, Type::Enum, &kReset, 0, false},
    {"link_drops", 6, Type::Unsigned, nullptr, 0, false},
    {"noise_dbm", 7, Type::Integer, nullptr, 0, false},
    {"duty_permille", 8, Type::Unsigned, nullptr, 0, false},
    {"rx_ok", 9, Type::Unsigned, nullptr, 0, false},
    {"rx_bad", 10, Type::Unsigned, nullptr, 0, false},
    {"rx_wait", 11, Type::Unsigned, nullptr, 0, false},
    {"rx_type", 12, Type::Unsigned, nullptr, 0, false},
    {"rx_unframed", 13, Type::Unsigned, nullptr, 0, false},
    {"rx_miskeyed", 14, Type::Unsigned, nullptr, 0, false},
    {"rx_noise", 15, Type::Unsigned, nullptr, 0, false},
    {"rx_named", 16, Type::Unsigned, nullptr, 0, false},
    {"tx_ok", 17, Type::Unsigned, nullptr, 0, false},
    {"tx_lost", 18, Type::Unsigned, nullptr, 0, false},
    {"tx_named", 19, Type::Unsigned, nullptr, 0, false},
    {"tx_keyed_us", 20, Type::Unsigned, nullptr, 0, false},
    {"tx_span_us", 21, Type::Unsigned, nullptr, 0, false},
    {"range_refused", 22, Type::Unsigned, nullptr, 0, false},
    {"dio1_armed", 23, Type::Boolean, nullptr, 0, false},
    {"dio1_edges", 24, Type::Unsigned, nullptr, 0, false},
    {"dio1_missed", 25, Type::Unsigned, nullptr, 0, false},
    {"tracked", 26, Type::Unsigned, nullptr, 0, false},
    {"alarm", 27, Type::Unsigned, nullptr, 0, false},
    {"fixes", 28, Type::Unsigned, nullptr, 0, false},
    {"valid", 29, Type::Boolean, nullptr, 0, false},
    {"baud", 30, Type::Unsigned, nullptr, 0, false},
    {"config", 31, Type::Enum, &kGnssConfig, 0, false},
    {"raises", 32, Type::Unsigned, nullptr, 0, false},
    {"raise_fallbacks", 33, Type::Unsigned, nullptr, 0, false},
    {"solution_phase_ms", 34, Type::Unsigned, nullptr, 0, false},
    {"overruns", 35, Type::Unsigned, nullptr, 0, false},
    {"identified", 36, Type::Boolean, nullptr, 0, false},
    {"firmware", 37, Type::Text, nullptr, 0, false},
    {"reject", 38, Type::Enum, &kReject, 0, false},
    {"rejected", 39, Type::Unsigned, nullptr, 0, false},
    {"residual_m", 40, Type::Unsigned, nullptr, 0, false},
    {"cell_mv", 41, Type::Unsigned, nullptr, 0, false},
    {"percent", 42, Type::Unsigned, nullptr, 0, false},
    {"battery_valid", 43, Type::Boolean, nullptr, 0, false},
    {"charging", 44, Type::Boolean, nullptr, 0, false},
    {"power_level", 45, Type::Enum, &kPowerLevel, 0, false},
    {"supply_warnings", 46, Type::Unsigned, nullptr, 0, false},
    {"implausible", 47, Type::Unsigned, nullptr, 0, false},
    {"die_temperature_c", 48, Type::Integer, nullptr, 0, false},
    {"charge", 49, Type::Enum, &kCharge, 0, false},
    {"charge_warnings", 50, Type::Unsigned, nullptr, 0, false},
    {"frames", 51, Type::Unsigned, nullptr, 0, false},
    {"frame_rate_hz", 52, Type::Unsigned, nullptr, 0, false},
    {"dropped", 54, Type::Unsigned, nullptr, 0, false},
    {"rearms", 55, Type::Unsigned, nullptr, 0, false},
    {"resets", 56, Type::Unsigned, nullptr, 0, false},
    {"command_errors", 57, Type::Unsigned, nullptr, 0, false},
    {"errors", 58, Type::Unsigned, nullptr, 0, false},
    {"rssi_dbm", 59, Type::UnsignedArray, nullptr, 0, false},
    {"crc", 60, Type::Unsigned, nullptr, 0, false},
    {"unsupported", 61, Type::Unsigned, nullptr, 0, false},
    {"folded", 62, Type::Unsigned, nullptr, 0, false},
    {"aircraft", 63, Type::Unsigned, nullptr, 0, false},
    {"tx_held_for_b5", 64, Type::Unsigned, nullptr, 0, false},
    {"rx_status", 65, Type::Unsigned, nullptr, 0, false},
    {"uplink_stale", 66, Type::Unsigned, nullptr, 0, false},
    {"tx_given_up", 67, Type::Unsigned, nullptr, 0, false},
    {"tx_deferred", 68, Type::Unsigned, nullptr, 0, false},
    {"tx_forced", 69, Type::Unsigned, nullptr, 0, false},
    {"tx_busy_missed", 70, Type::Unsigned, nullptr, 0, false},
    {"armed", 71, Type::Boolean, nullptr, 0, false},
    {"profile", 72, Type::Enum, &kCaptureProfile, 0, false},
    {"session", 73, Type::Unsigned, nullptr, 0, false},
    {"records", 74, Type::Unsigned, nullptr, 0, false},
    {"records_dropped", 75, Type::Unsigned, nullptr, 0, false},
    {"sectors", 76, Type::Unsigned, nullptr, 0, false},
    {"keeps_s", 77, Type::Unsigned, nullptr, 0, false},
    {"takes_sectors", 78, Type::Unsigned, nullptr, 0, false},
    {"evicts_flights", 79, Type::Unsigned, nullptr, 0, false},
    {"keeps_full_s", 80, Type::Unsigned, nullptr, 0, false},
    {"keeps_power_run_s", 81, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kUpdateKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"image", 1, Type::Enum, &kImage, 0, true},
    {"from", 2, Type::Text, nullptr, 0, false},
    {"to", 3, Type::Text, nullptr, 0, false},
    {"settings", 4, Type::Enum, &kSettingsFallback, 0, false},
    {"swap_powered", 5, Type::Boolean, nullptr, 0, true},
    {"imu", 6, Type::Enum, &kHubImage, 0, true},
    {"imu_digest", 7, Type::Bytes, nullptr, 8, false},
    {"downgrade", 8, Type::Boolean, nullptr, 0, false},
    {"key", 9, Type::Bytes, nullptr, 4, false},
    {"product", 10, Type::Enum, &kProduct, 0, false},
};

inline constexpr Key kDfuKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"version", 1, Type::Text, nullptr, 0, true},
};

inline constexpr Key kRecoveryKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
};

inline constexpr Key kPowerOffKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
};

inline constexpr Key kChallengeKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"nonce", 1, Type::Bytes, nullptr, 16, true},
    {"server_key_id", 2, Type::Bytes, nullptr, 4, true},
};

inline constexpr Key kRegisterKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"server_nonce", 1, Type::Bytes, nullptr, 16, true},
    {"signature", 2, Type::Bytes, nullptr, 64, true},
};

inline constexpr Key kRegistrationKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"device_id", 1, Type::Bytes, nullptr, 8, true},
    {"public_key", 2, Type::Bytes, nullptr, 32, true},
    {"version", 3, Type::Text, nullptr, 0, true},
    {"server_key_id", 4, Type::Bytes, nullptr, 4, true},
    {"release", 5, Type::Boolean, nullptr, 0, true},
    {"signature", 6, Type::Bytes, nullptr, 64, true},
};

inline constexpr Key kUnlockKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"serial", 1, Type::Unsigned, nullptr, 0, true},
    {"signature", 2, Type::Bytes, nullptr, 64, true},
};

inline constexpr Key kListKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
    {"index", 2, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kReadKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
    {"session", 2, Type::Unsigned, nullptr, 0, true},
    {"from", 3, Type::Unsigned, nullptr, 0, false},
    {"count", 4, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kEraseKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
};

inline constexpr Key kLogKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
    {"acknowledge", 2, Type::Boolean, nullptr, 0, true},
    {"reason", 3, Type::Enum, &kReason, 0, false},
    {"sessions", 4, Type::Unsigned, nullptr, 0, false},
    {"truncated", 5, Type::Boolean, nullptr, 0, false},
};

inline constexpr Key kSessionKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
    {"index", 2, Type::Unsigned, nullptr, 0, true},
    {"of", 3, Type::Unsigned, nullptr, 0, true},
    {"session", 4, Type::Unsigned, nullptr, 0, true},
    {"records", 5, Type::Unsigned, nullptr, 0, true},
    {"closed", 6, Type::Boolean, nullptr, 0, true},
    {"truncated", 7, Type::Boolean, nullptr, 0, true},
};

inline constexpr Key kChunkKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"log", 1, Type::Enum, &kLog, 0, false},
    {"session", 2, Type::Unsigned, nullptr, 0, true},
    {"from", 3, Type::Unsigned, nullptr, 0, true},
    {"ends_session", 4, Type::Boolean, nullptr, 0, true},
    {"records", 5, Type::Bytes, nullptr, 0, true},
};

inline constexpr Key kGnssColdStartKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
};

inline constexpr Key kOwnshipKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"aircraft_type", 1, Type::Unsigned, nullptr, 0, true},
    {"width", 2, Type::Unsigned, nullptr, 0, true},
    {"hot_spot", 3, Type::Unsigned, nullptr, 0, true},
    {"rows", 4, Type::Bytes, nullptr, 0, true},
};

inline constexpr Key kObservationKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"address", 1, Type::Unsigned, nullptr, 0, true},
    {"boot_epoch", 2, Type::Unsigned, nullptr, 0, true},
    {"sequence", 3, Type::Unsigned, nullptr, 0, true},
    {"at_s", 4, Type::Unsigned, nullptr, 0, true},
    {"into_ms", 5, Type::Unsigned, nullptr, 0, false},
    {"latitude_1e7", 6, Type::Integer, nullptr, 0, false},
    {"longitude_1e7", 7, Type::Integer, nullptr, 0, false},
    {"altitude_m", 8, Type::Integer, nullptr, 0, false},
    {"altitude_age_ms", 9, Type::Integer, nullptr, 0, false},
    {"pressure_altitude_ft", 10, Type::Integer, nullptr, 0, false},
    {"pressure_altitude_age_ms", 11, Type::Integer, nullptr, 0, false},
    {"speed_q", 12, Type::Unsigned, nullptr, 0, false},
    {"track_c9", 13, Type::Unsigned, nullptr, 0, false},
    {"speed_age_ms", 14, Type::Integer, nullptr, 0, false},
    {"climb_e8", 15, Type::Integer, nullptr, 0, false},
    {"climb_source", 16, Type::Enum, &kClimbSource, 0, false},
    {"climb_age_ms", 17, Type::Integer, nullptr, 0, false},
    {"flight_state", 18, Type::Enum, &kFlight, 0, false},
    {"flight_state_age_ms", 19, Type::Integer, nullptr, 0, false},
    {"aircraft_category", 20, Type::Unsigned, nullptr, 0, false},
    {"adsb_version", 21, Type::Unsigned, nullptr, 0, false},
    {"gva", 22, Type::Unsigned, nullptr, 0, false},
    {"callsign", 23, Type::Text, nullptr, 0, false},
    {"callsign_heard_s", 24, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kHealthKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"protocol_version", 1, Type::Unsigned, nullptr, 0, true},
    {"boot_epoch", 2, Type::Unsigned, nullptr, 0, true},
    {"listening", 3, Type::Boolean, nullptr, 0, true},
    {"utc_valid", 4, Type::Boolean, nullptr, 0, true},
    {"last_sequence", 5, Type::Unsigned, nullptr, 0, false},
    {"own_icao_set", 6, Type::Boolean, nullptr, 0, true},
    {"alarm_granted", 7, Type::Boolean, nullptr, 0, true},
    {"export_reference", 11, Type::Enum, &kExportReference, 0, true},
    {"paired", 12, Type::Boolean, nullptr, 0, true},
    {"own_transponder", 13, Type::Unsigned, nullptr, 0, false},
    {"own_transponder_callsign", 14, Type::Text, nullptr, 0, false},
    {"own_transponder_category", 15, Type::Unsigned, nullptr, 0, false},
    {"firmware", 16, Type::Text, nullptr, 0, false},
    {"battery_percent", 17, Type::Unsigned, nullptr, 0, false},
    {"charging", 18, Type::Boolean, nullptr, 0, true},
    {"power_level", 19, Type::Enum, &kPowerLevel, 0, true},
    {"frame_rate_hz", 20, Type::Unsigned, nullptr, 0, false},
};

inline constexpr Key kPeerClaimKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"boot_epoch", 1, Type::Unsigned, nullptr, 0, true},
    {"at_s", 2, Type::Unsigned, nullptr, 0, false},
    {"into_ms", 3, Type::Unsigned, nullptr, 0, false},
    {"latitude_1e7", 4, Type::Integer, nullptr, 0, false},
    {"longitude_1e7", 5, Type::Integer, nullptr, 0, false},
    {"position_age_ms", 6, Type::Unsigned, nullptr, 0, false},
    {"altitude_hae_mm", 7, Type::Integer, nullptr, 0, false},
    {"altitude_hae_age_ms", 8, Type::Unsigned, nullptr, 0, false},
    {"pressure_altitude_mm", 9, Type::Integer, nullptr, 0, false},
    {"pressure_altitude_age_ms", 10, Type::Unsigned, nullptr, 0, false},
    {"own_icao", 11, Type::Unsigned, nullptr, 0, false},
    {"alarm_requested", 12, Type::Boolean, nullptr, 0, true},
    {"paired", 18, Type::Boolean, nullptr, 0, true},
};

inline constexpr Key kCaptureKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"profile", 1, Type::Enum, &kCaptureProfile, 0, true},
};

inline constexpr Key kBrowserChallengeKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"nonce", 1, Type::Bytes, nullptr, 16, true},
};

inline constexpr Key kTrustBrowserKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"key", 1, Type::Bytes, nullptr, 32, true},
    {"signature", 2, Type::Bytes, nullptr, 64, true},
    {"label", 3, Type::Text, nullptr, 0, false},
};

inline constexpr Key kBrowsersKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"index", 1, Type::Unsigned, nullptr, 0, true},
};

inline constexpr Key kBrowserKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false}, {"index", 1, Type::Unsigned, nullptr, 0, true},
    {"count", 2, Type::Unsigned, nullptr, 0, true},  {"id", 3, Type::Bytes, nullptr, 8, false},
    {"label", 4, Type::Text, nullptr, 0, false},
};

inline constexpr Key kForgetBrowserKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"id", 1, Type::Bytes, nullptr, 8, true},
};

inline constexpr Key kInstallReportKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"check", 1, Type::Enum, &kStagedCheck, 0, false},
    {"swap", 2, Type::Enum, &kSwapType, 0, false},
    {"request_error", 3, Type::Integer, nullptr, 0, false},
    {"boots", 4, Type::Unsigned, nullptr, 0, false},
    {"probation_boots", 5, Type::Unsigned, nullptr, 0, false},
    {"proof", 6, Type::Enum, &kProof, 0, false},
    {"reset", 7, Type::Enum, &kReset, 0, false},
    {"landed_check", 8, Type::Enum, &kStagedCheck, 0, false},
    {"staged_key", 9, Type::Bytes, nullptr, 4, false},
};

inline constexpr Key kProtocolsKeys[] = {
    {"command", 0, Type::Enum, &kCommand, 0, false},
    {"adsb_version", 1, Type::Text, nullptr, 0, false},
    {"adsl_version", 2, Type::Text, nullptr, 0, false},
    {"alptas_version", 3, Type::Text, nullptr, 0, false},
};

inline constexpr Frame kFrames[] = {
    {"acknowledgement", 0, events::Endpoint::Config, Sender::Unit, kAcknowledgementKeys, 5},
    {"get", 1, events::Endpoint::Config, Sender::Host, kGetKeys, 1},
    {"config", 2, events::Endpoint::Config, Sender::Unit, kConfigKeys, 12},
    {"defaults", 3, events::Endpoint::Config, Sender::Both, kConfigKeys, 12},
    {"set", 4, events::Endpoint::Config, Sender::Host, kConfigKeys, 12},
    {"status", 5, events::Endpoint::Config, Sender::Both, kStatusKeys, 9},
    {"timing", 6, events::Endpoint::Config, Sender::Both, kTimingKeys, 16},
    {"flash", 7, events::Endpoint::Config, Sender::Both, kFlashKeys, 9},
    {"radio", 8, events::Endpoint::Config, Sender::Both, kDiagnosticsKeys, 81},
    {"diagnostics", 9, events::Endpoint::Config, Sender::Both, kDiagnosticsKeys, 81},
    {"update", 10, events::Endpoint::Config, Sender::Both, kUpdateKeys, 11},
    {"dfu", 11, events::Endpoint::Config, Sender::Host, kDfuKeys, 2},
    {"recovery", 12, events::Endpoint::Config, Sender::Host, kRecoveryKeys, 1},
    {"power_off", 13, events::Endpoint::Config, Sender::Host, kPowerOffKeys, 1},
    {"challenge", 14, events::Endpoint::Config, Sender::Both, kChallengeKeys, 3},
    {"register", 15, events::Endpoint::Config, Sender::Host, kRegisterKeys, 3},
    {"registration", 16, events::Endpoint::Config, Sender::Unit, kRegistrationKeys, 7},
    {"unlock", 17, events::Endpoint::Config, Sender::Host, kUnlockKeys, 3},
    {"list", 18, events::Endpoint::Log, Sender::Host, kListKeys, 3},
    {"read", 19, events::Endpoint::Log, Sender::Host, kReadKeys, 5},
    {"erase", 20, events::Endpoint::Log, Sender::Host, kEraseKeys, 2},
    {"log", 21, events::Endpoint::Log, Sender::Unit, kLogKeys, 6},
    {"session", 22, events::Endpoint::Log, Sender::Unit, kSessionKeys, 8},
    {"chunk", 23, events::Endpoint::Log, Sender::Unit, kChunkKeys, 6},
    {"gnss_cold_start", 24, events::Endpoint::Config, Sender::Host, kGnssColdStartKeys, 1},
    {"ownship", 25, events::Endpoint::Config, Sender::Host, kOwnshipKeys, 5},
    {"observation", 26, events::Endpoint::Peer, Sender::Unit, kObservationKeys, 25},
    {"health", 27, events::Endpoint::Peer, Sender::Unit, kHealthKeys, 18},
    {"peer_claim", 28, events::Endpoint::Peer, Sender::Host, kPeerClaimKeys, 14},
    {"capture", 29, events::Endpoint::Config, Sender::Host, kCaptureKeys, 2},
    {"browser_challenge", 30, events::Endpoint::Config, Sender::Both, kBrowserChallengeKeys, 2},
    {"trust_browser", 31, events::Endpoint::Config, Sender::Host, kTrustBrowserKeys, 4},
    {"browsers", 32, events::Endpoint::Config, Sender::Host, kBrowsersKeys, 2},
    {"browser", 33, events::Endpoint::Config, Sender::Unit, kBrowserKeys, 5},
    {"forget_browser", 34, events::Endpoint::Config, Sender::Host, kForgetBrowserKeys, 2},
    {"install_report", 35, events::Endpoint::Config, Sender::Both, kInstallReportKeys, 10},
    {"protocols", 36, events::Endpoint::Config, Sender::Both, kProtocolsKeys, 4},
};

}

#endif
