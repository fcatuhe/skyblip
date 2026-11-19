// hardware/parts/l76k/model_commands.h: what the L76K model does with each $PCAS sentence it hears.
#ifndef SKYBLIP_HARDWARE_MODEL_L76K_COMMANDS_H
#define SKYBLIP_HARDWARE_MODEL_L76K_COMMANDS_H

#include "hardware/parts/l76k/model.h"

namespace skyblip::models {

inline void L76k::apply_command() {
    commands_seen++;
    if (!gnss::nmea_checksum_ok(command_, command_length_)) {
        commands_rejected++;
        return;
    }
    for (int i = 1; i < command_length_ && i < 7; i++) heard_ += command_[i];
    heard_ += ',';

    // The version banner is the part naming itself, not a setting: a
    // receiver that obeys nothing still answers it.
    if (starts_with(command_, command_length_, "$PCAS06,")) {
        if (answers_identification) emit_version();
        return;
    }
    if (!accepts_commands) return;
    if (starts_with(command_, command_length_, "$PCAS01,"))
        apply_baud(static_cast<int>(argument(command_, command_length_, 8)));
    else if (starts_with(command_, command_length_, "$PCAS04,"))
        constellations = static_cast<uint8_t>(argument(command_, command_length_, 8));
    else if (starts_with(command_, command_length_, "$PCAS03,"))
        apply_sentence_set();
    else if (starts_with(command_, command_length_, "$PCAS11,"))
        dynamic_model = static_cast<uint8_t>(argument(command_, command_length_, 8));
    else if (starts_with(command_, command_length_, "$PCAS02,"))
        apply_rate(argument(command_, command_length_, 8));
    else if (starts_with(command_, command_length_, "$PCAS10,"))
        apply_restart(static_cast<int>(argument(command_, command_length_, 8)));
}

// INFO: fc 19sep26 $PCAS01 numbers the rates 0..5, L76K protocol spec V1.1 SS2.3.1
inline void L76k::apply_baud(int code) {
    static constexpr uint32_t kRates[6] = {4800, 9600, 19200, 38400, 57600, 115200};
    if (refuses_baud_command || code < 0 || code >= 6) return;
    baud = kRates[code];
}

// INFO: fc 18nov26 CASIC V4.2.0.3 1.6.3's intervals, and under 1000 ms only at 115200 (V1.1)
inline void L76k::apply_rate(uint32_t interval_ms) {
    if (interval_ms == parts::L76k::kSolutionPeriodMs) quarter_rate_asked++;
    if (interval_ms < kFactoryPeriodMs) sub_second_rate_asked++;
    if (interval_ms == parts::L76k::kSolutionPeriodMs && hangs_on_quarter_rate) {
        hung = true;
        pending_.clear();
        said_.clear();
        return;
    }
    if (interval_ms != 1000 && interval_ms != 500 && interval_ms != 250 && interval_ms != 200 &&
        interval_ms != 100)
        return;
    if (interval_ms < kFactoryPeriodMs &&
        (baud != parts::L76k::kTargetBaudRate || refuses_quarter_rate ||
         interval_ms < fastest_interval_ms))
        return;
    if (interval_ms < kFactoryPeriodMs && quarter_rate_runs_at_ms != 0)
        interval_ms = quarter_rate_runs_at_ms;
    solution_period_ms = interval_ms;
}

// INFO: fc 18sep26 $PCAS03 fields are GGA,GLL,GSA,GSV,RMC,VTG and an empty one keeps its
// setting
inline void L76k::apply_sentence_set() {
    static constexpr int kGsaField = 2;
    static constexpr int kGsvField = 3;
    bool* flags[6] = {&gga_enabled, &gll_enabled, nullptr, nullptr, &rmc_enabled, &vtg_enabled};
    int field = 0;
    int at = 8;
    while (at < command_length_ && field < 6) {
        const bool empty = command_[at] == ',' || command_[at] == '*';
        const uint8_t every = static_cast<uint8_t>(argument(command_, command_length_, at));
        if (!empty && field == kGsaField) gsa_every = every;
        if (!empty && field == kGsvField) gsv_every = every;
        if (!empty && flags[field] != nullptr) *flags[field] = every != 0;
        while (at < command_length_ && command_[at] != ',') at++;
        at++;
        field++;
    }
    sentence_set_applied = true;
}

// $PCAS10,n: 0 hot, 1 warm, 2 cold, 3 factory. The module reboots either
// way; a cold start also throws the orbit data away, and a factory reset
// takes every setting we applied with it.
inline void L76k::apply_restart(int kind) {
    restarts++;
    last_restart_kind = kind;
    reboot_since_ms_ = last_tick_ms_;
    rebooting_ = true;
    pending_.clear();
    said_.clear();
    if (kind >= 2) {
        cold_since_ms_ = last_tick_ms_;
        cold_ = true;
    }
    if (kind < 3) return;
    factory_resets++;
    factory_settings();
}

inline void L76k::factory_settings() {
    constellations = 0;
    dynamic_model = 0;
    sentence_set_applied = false;
    gga_enabled = gll_enabled = true;
    rmc_enabled = vtg_enabled = true;
    gsa_every = 1;
    gsv_every = 1;
    solution_period_ms = kFactoryPeriodMs;
}

// INFO: fc 18nov26 V_BCKP keeps the almanac, so a reset part boots warm on factory settings
inline void L76k::reset_pulse() {
    hardware_resets++;
    hung = false;
    baud = parts::L76k::kFactoryBaudRate;
    reboot_since_ms_ = last_tick_ms_;
    rebooting_ = true;
    pending_.clear();
    said_.clear();
    factory_settings();
}

inline void L76k::set(int pin, bool level) {
    if (pin != reset_pin) return;
    if (!level) {
        if (!held_in_reset_) reset_since_ms_ = last_tick_ms_;
        held_in_reset_ = true;
        pending_.clear();
        said_.clear();
        return;
    }
    if (!held_in_reset_) return;
    held_in_reset_ = false;
    if (last_tick_ms_ - reset_since_ms_ >= kResetLowMs) reset_pulse();
}

}  // namespace skyblip::models

#endif
