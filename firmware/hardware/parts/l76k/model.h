#ifndef SKYBLIP_HARDWARE_MODEL_L76K_H
#define SKYBLIP_HARDWARE_MODEL_L76K_H

#include <deque>
#include <string>

#include "core/gnss/nmea.h"
#include "hardware/io/io.h"
#include "hardware/parts/gnss_truth.h"
#include "hardware/parts/l76k/l76k.h"

namespace skyblip::models {

// INFO: fc 20nov26 the model is also the wire, so a rate mismatch frames rubbish both ways, as on
// silicon
class L76k : public io::Uart, public io::UartRate, public io::Gpio, public GnssTruth {
   public:
    bool fix_valid{true};
    uint8_t satellites{9};
    bool emit_geoid_separation{true};
    // INFO: fc 22nov26 L76K V1.1 leaves RMC's SOG and COG "empty in case of an invalid value"
    bool emit_speed{true};
    bool emit_course{true};
    uint8_t gps_in_view{9};
    uint8_t beidou_in_view{7};
    uint8_t glonass_in_view{5};
    uint8_t cn0_dbhz_base{44};
    uint16_t hdop_e2{90};
    uint16_t vdop_e2{150};
    uint16_t pdop_e2{180};

    // INFO: gn 09Jun25 the AT6558 never acknowledges a $PCAS sentence, so only its changed
    // behaviour shows it landed
    static constexpr int kGsaSlots = 12;
    static constexpr uint32_t kFactoryPeriodMs = 1000;
    static constexpr uint8_t kAviationDynamicModel = 6;

    bool accepts_commands{true};

    // INFO: fc 03aug26 the L76K wakes on UART activity and stays deaf for about half a second
    static constexpr uint32_t kWakeMs = 500;
    bool asleep{false};

    uint32_t baud{parts::L76k::kFactoryBaudRate};
    bool refuses_baud_command{false};
    bool refuses_fast_rate{false};
    // INFO: fc 18nov26 a part that runs a sub-second $PCAS02 at another interval, as stamps show
    uint32_t fast_rate_runs_at_ms{0};
    // INFO: fc 18nov26 the shortest $PCAS02 interval the part takes, so 1000 refuses 2 Hz
    uint32_t fastest_interval_ms{100};
    // INFO: fc 18nov26 a part that lost UTC sends a no-time GGA and RMC, LilyGO-Modem-Series #95
    bool time_lost{false};
    // INFO: fc 18nov26 Go 000001 stopped NMEA and PPS together at 22:07, and only a reboot cured it
    bool hung{false};
    bool hangs_on_fast_rate{false};
    uint32_t sub_second_rate_asked{0};
    // INFO: fc 18nov26 Quectel L76K Hardware Design V1.0 4.2.1: RESET_N low at least 100 ms
    static constexpr uint32_t kResetLowMs = 100;
    int reset_pin{-1};
    uint32_t hardware_resets{0};

    // INFO: fc 03aug26 $PCAS06 is answered with the CASIC firmware banner, which a clone may never
    // send
    bool answers_identification{true};
    const char* firmware_version{"URANUS5,V5.1.0.0"};

    // INFO: fc 20nov26 a cold start blinds the receiver until a fresh almanac, compressed here to
    // the datasheet's cold TTFF
    static constexpr uint32_t kColdStartTtffMs = 30000;
    static constexpr uint32_t kRebootMs = 300;

    // INFO: fc 18sep26 a cold receiver's solutions walk: metres of error at the first fix, decaying
    // | 18nov26 and the sign turns each whole second, where own-ship measures the residual
    uint32_t walk_amplitude_m{0};
    uint32_t walk_duration_ms{15000};

    uint32_t solution_period_ms{kFactoryPeriodMs};
    // INFO: fc 18nov26 how long after its solution the part starts talking: SoftRF measured 70 ms
    uint32_t latency_ms{0};
    // INFO: fc 18nov26 a UART stall: what lands in [from, until) of each period lands at until
    uint32_t stall_period_ms{0};
    uint32_t stall_from_ms{0};
    uint32_t stall_until_ms{0};
    uint8_t constellations{0};
    uint8_t dynamic_model{0};
    bool sentence_set_applied{false};
    // INFO: fc 13sep26 the factory set, which is why a receiver that obeys nothing is still heard
    bool gga_enabled{true};
    bool gll_enabled{true};
    // INFO: fc 29sep26 a $PCAS03 field counts solutions per sentence and 0 is off
    uint8_t gsa_every{1};
    uint8_t gsv_every{1};
    bool rmc_enabled{true};
    bool vtg_enabled{true};
    uint32_t commands_seen{0};
    uint32_t commands_rejected{0};
    uint32_t wake_bytes{0};
    uint32_t deaf_bytes{0};
    uint32_t garbled_bytes{0};
    uint32_t baud_changes{0};
    uint32_t restarts{0};
    uint32_t factory_resets{0};
    int last_restart_kind{-1};

    struct Solved {
        uint32_t at_ms{0};
        uint32_t utc_seconds_of_day{0};
        uint16_t utc_ms{0};
        int32_t latitude_1e7{0};
        int32_t longitude_1e7{0};
        int32_t altitude_hae_mm{0};
    };
    static constexpr int kSolvedKept = 256;
    int solved_count() const { return solved_count_ < kSolvedKept ? solved_count_ : kSolvedKept; }
    const Solved& solved(int back) const {
        return solved_[(solved_count_ - 1 - back + kSolvedKept) % kSolvedKept];
    }

    bool aviation_dynamic_model() const { return dynamic_model == kAviationDynamicModel; }
    bool gsa_enabled() const { return gsa_every != 0; }
    bool gsv_enabled() const { return gsv_every != 0; }

    const std::string& heard() const { return heard_; }

    bool set(uint32_t rate) override {
        if (port_baud_ == rate) return true;
        port_baud_ = rate;
        baud_changes++;
        return true;
    }
    uint32_t port_baud() const { return port_baud_; }

    void set(int pin, bool level) override;
    bool get(int) override { return false; }
    void mode_output(int) override {}
    void mode_input(int, bool) override {}

    size_t write(const uint8_t* data, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) {
            if (port_baud_ != baud) {
                garbled_bytes++;
                continue;
            }
            if (rebooting() || hung || held_in_reset_) continue;
            if (asleep) {
                asleep = false;
                deaf_since_ms_ = last_tick_ms_;
                deaf_ = true;
                wake_bytes++;
                heard_ += "WAKE,";
                continue;
            }
            if (deaf()) {
                deaf_bytes++;
                command_length_ = 0;
                continue;
            }
            const char character = static_cast<char>(data[i]);
            if (character == '$') {
                command_length_ = 0;
                command_[command_length_++] = character;
            } else if (character == '\r' || character == '\n') {
                if (command_length_ > 0) apply_command();
                command_length_ = 0;
            } else if (command_length_ > 0 && command_length_ < kCommandBytes - 1) {
                command_[command_length_++] = character;
            }
        }
        return bytes;
    }
    size_t read(uint8_t* out, size_t capacity) override {
        // INFO: fc 20nov26 sentences sent at a mismatched rate are lost, not queued until the rate
        // is fixed
        if (port_baud_ != baud) {
            pending_.clear();
            return 0;
        }
        size_t count = pending_.size() < capacity ? pending_.size() : capacity;
        for (size_t i = 0; i < count; i++) out[i] = static_cast<uint8_t>(pending_[i]);
        pending_.erase(0, count);
        return count;
    }
    size_t available() override { return pending_.size(); }

    // INFO: fc 18nov26 solutions fall where the receiver's UTC is a whole multiple of its period
    void tick(uint32_t now_ms) {
        last_tick_ms_ = now_ms;
        // INFO: fc 20nov26 a flag, not last_ms_ == 0, marks the first tick, because t=0 is a legal
        // timestamp
        if (!armed_) {
            armed_ = true;
            last_ms_ = now_ms;
            align_utc_ms(now_ms);
            return;
        }
        const int32_t elapsed_ms = static_cast<int32_t>(now_ms - last_ms_);
        if (elapsed_ms <= 0) return;
        last_ms_ = now_ms;
        int32_t left_ms = elapsed_ms;
        for (;;) {
            const int32_t to_solution_ms =
                static_cast<int32_t>(solution_period_ms - utc_ms() % solution_period_ms);
            if (to_solution_ms > left_ms) break;
            fly(to_solution_ms);
            left_ms -= to_solution_ms;
            solve(now_ms - static_cast<uint32_t>(left_ms));
        }
        fly(left_ms);
        while (!said_.empty() && static_cast<int32_t>(now_ms - said_.front().at_ms) >= 0) {
            pending_ += said_.front().text;
            said_.pop_front();
        }
    }

   private:
    static constexpr int kCommandBytes = 64;

    char command_[kCommandBytes]{};
    int command_length_{0};
    // INFO: fc 20nov26 start instants and flags, not end instants, because an end compared with <
    // inverts across the 49.7-day wrap
    uint32_t last_tick_ms_{0};
    uint32_t deaf_since_ms_{0};
    uint32_t reboot_since_ms_{0};
    uint32_t cold_since_ms_{0};
    bool deaf_{false};
    bool rebooting_{false};
    bool cold_{false};
    uint32_t port_baud_{parts::L76k::kFactoryBaudRate};
    uint32_t solving_since_ms_{0};
    uint32_t walk_step_{0};
    uint32_t solutions_{0};
    bool solving_since_set_{false};
    std::string heard_;
    Solved solved_[kSolvedKept]{};
    int solved_count_{0};
    struct Said {
        uint32_t at_ms{0};
        std::string text;
    };
    std::deque<Said> said_;
    std::string saying_;
    bool solving_now_{false};

    void solve(uint32_t at_ms) {
        if (asleep || rebooting() || hung || held_in_reset_) return;
        saying_.clear();
        solving_now_ = true;
        emit_burst();
        solving_now_ = false;
        solved_[solved_count_ % kSolvedKept] = Solved{at_ms,
                                                      utc_seconds_of_day,
                                                      static_cast<uint16_t>(utc_ms()),
                                                      walked_latitude_1e7(),
                                                      longitude_1e7,
                                                      altitude_mm()};
        solved_count_++;
        said_.push_back(Said{heard_at(at_ms + latency_ms), saying_});
    }

    uint32_t heard_at(uint32_t ready_ms) const {
        if (stall_period_ms == 0) return ready_ms;
        const uint32_t phase_ms = ready_ms % stall_period_ms;
        if (phase_ms < stall_from_ms || phase_ms >= stall_until_ms) return ready_ms;
        return ready_ms - phase_ms + stall_until_ms;
    }

    void say(const char* line, int bytes) {
        (solving_now_ ? saying_ : pending_).append(line, static_cast<size_t>(bytes));
    }

    // INFO: fc 18nov26 the part stamps hhmmss.sss (L76K V1.1 table 3), the solved quarter included
    int put_stamp(char* out) const {
        int written = put_time(out);
        out[written++] = '.';
        return written + format_unsigned(out + written, utc_ms(), 3);
    }

    static bool starts_with(const char* text, int bytes, const char* prefix) {
        int i = 0;
        for (; prefix[i]; i++) {
            if (i >= bytes || text[i] != prefix[i]) return false;
        }
        return true;
    }
    static uint32_t argument(const char* text, int bytes, int at) {
        uint32_t value = 0;
        for (int i = at; i < bytes && text[i] >= '0' && text[i] <= '9'; i++)
            value = value * 10 + (text[i] - '0');
        return value;
    }

    void apply_command();
    void apply_baud(int code);
    void apply_rate(uint32_t interval_ms);
    void apply_sentence_set();
    void apply_restart(int kind);
    void reset_pulse();
    void factory_settings();
    bool held_in_reset_{false};
    uint32_t reset_since_ms_{0};

    int32_t walk_now_m() const {
        if (walk_amplitude_m == 0 || !solving_since_set_) return 0;
        const uint32_t elapsed = last_tick_ms_ - solving_since_ms_;
        if (elapsed >= walk_duration_ms) return 0;
        const uint32_t amplitude =
            walk_amplitude_m * (walk_duration_ms - elapsed) / walk_duration_ms;
        return static_cast<int32_t>(amplitude) * (walk_step_ % 2 == 0 ? 1 : -1);
    }

    int32_t walked_latitude_1e7() const {
        return latitude_1e7 + static_cast<int32_t>(walk_now_m() * 1e7 / 111320.0);
    }

    void step_walk() {
        if (!solving()) {
            solving_since_set_ = false;
            return;
        }
        if (!solving_since_set_) {
            solving_since_ms_ = last_tick_ms_;
            solving_since_set_ = true;
            walk_step_ = 0;
            return;
        }
        if (utc_ms() == 0) walk_step_++;
    }

    bool deaf() const { return deaf_ && last_tick_ms_ - deaf_since_ms_ < kWakeMs; }
    bool rebooting() const { return rebooting_ && last_tick_ms_ - reboot_since_ms_ < kRebootMs; }
    bool cold() const { return cold_ && last_tick_ms_ - cold_since_ms_ < kColdStartTtffMs; }
    bool solving() const { return fix_valid && !cold(); }

    void emit_burst();
    void emit_version();
    void emit_rmc();
    void emit_gsa();
    static uint8_t share_of_solution(uint8_t budget, uint8_t in_view, uint8_t total_in_view);
    void emit_gsa_for(uint8_t system_id, uint8_t first_id, uint8_t in_view, uint8_t budget);
    void emit_gll();
    void emit_gsv();
    void emit_gsv_set(const char* talker, uint8_t in_view, uint8_t first_id);
    uint8_t tracked_of(uint8_t in_view) const;
    void emit_vtg();
    void emit_gga();

    std::string pending_;
    uint32_t last_ms_{0};
    bool armed_{false};
};

}  // namespace skyblip::models

#include "hardware/parts/l76k/model_commands.h"
#include "hardware/parts/l76k/model_sentences.h"

#endif
