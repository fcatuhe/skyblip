#ifndef SKYBLIP_HARDWARE_MODEL_AG3335_H
#define SKYBLIP_HARDWARE_MODEL_AG3335_H

#include <string>

#include "core/gnss/nmea.h"
#include "hardware/io/io.h"
#include "hardware/parts/ag3335/pair.h"
#include "hardware/parts/gnss_truth.h"

namespace skyblip::models {

class Ag3335 : public io::Uart, public GnssTruth {
   public:
    static constexpr int kSentenceTypes = 7;
    static constexpr int kGga = 0;
    static constexpr int kGll = 1;
    static constexpr int kGsa = 2;
    static constexpr int kGsv = 3;
    static constexpr int kRmc = 4;
    static constexpr int kVtg = 5;
    static constexpr int kZda = 6;
    static constexpr uint32_t kFactoryPeriodMs = 1000;
    static constexpr int kGsaSlots = 12;

    static constexpr uint8_t kGps = 1u << 0;
    static constexpr uint8_t kGlonass = 1u << 1;
    static constexpr uint8_t kGalileo = 1u << 2;
    static constexpr uint8_t kBeidou = 1u << 3;
    static constexpr uint8_t kQzss = 1u << 4;
    static constexpr uint8_t kNavic = 1u << 5;

    static constexpr int kPpsUnstated = -1;

    bool fix_valid{true};
    uint8_t satellites{14};
    uint16_t hdop_e2{70};
    uint16_t vdop_e2{110};
    uint16_t pdop_e2{130};
    bool emit_geoid_separation{true};

    bool answers{true};
    uint32_t acks_to_drop{0};
    uint16_t unsupported_command{0};
    // INFO: fc 11nov26 result 1 means still processing and the final result follows, SIM65M §2.3.1
    uint16_t processing_command{0};
    uint32_t processing_ms{0};
    // INFO: fc 11nov26 SIM65M §2.3.36 restarts on $PAIR066 for a time no document states
    uint32_t restart_ms{0};

    // INFO: fc 11nov26 an X1 answered AG3335M_V3.3.0 in Meshtastic PR #11889
    const char* project_version{"AG3335M_V3.3.0"};
    char frequency{'D'};

    // INFO: fc 11nov26 the factory set is all seven sentences, Meshtastic issue #4626 on a T1000-E
    uint8_t sentence_every[kSentenceTypes]{1, 1, 1, 1, 1, 1, 1};
    uint32_t solution_period_ms{kFactoryPeriodMs};
    uint8_t constellations{0};
    // INFO: fc 11nov26 SIM65M §2.3.50 makes fitness the default, though §2.3.51 says normal
    uint8_t navigation_mode{1};
    uint8_t nmea_mode{1};
    uint8_t dgps_mode{2};
    bool sbas_enabled{true};
    bool sleep_locked{false};
    int pps_type{kPpsUnstated};
    uint16_t pps_width_ms{0};

    uint32_t commands_seen{0};
    uint32_t commands_rejected{0};
    uint32_t restarts{0};
    uint32_t factory_resets{0};
    int last_restart_kind{-1};

    bool sentence_on(int type) const { return sentence_every[type] != 0; }
    bool tracks_l5() const {
        const uint8_t l5 = kGps | kGalileo | kBeidou;
        return frequency == 'D' && (constellations & l5) == l5 && (constellations & kNavic) == 0;
    }

    const std::string& heard() const { return heard_; }

    size_t write(const uint8_t* data, size_t bytes) override {
        for (size_t i = 0; i < bytes; i++) {
            const char character = static_cast<char>(data[i]);
            if (character == '$') {
                command_length_ = 0;
                command_[command_length_++] = character;
            } else if (character == '\r' || character == '\n') {
                if (command_length_ > 0) take_command();
                command_length_ = 0;
            } else if (command_length_ > 0 && command_length_ < kCommandBytes - 1) {
                command_[command_length_++] = character;
            }
        }
        return bytes;
    }
    size_t read(uint8_t* out, size_t capacity) override {
        const size_t count = pending_.size() < capacity ? pending_.size() : capacity;
        for (size_t i = 0; i < count; i++) out[i] = static_cast<uint8_t>(pending_[i]);
        pending_.erase(0, count);
        return count;
    }
    size_t available() override { return pending_.size(); }

    void tick(uint32_t now_ms) {
        last_tick_ms_ = now_ms;
        if (!armed_) {
            armed_ = true;
            last_ms_ = now_ms;
            report_ms_ = now_ms;
            return;
        }
        const int32_t elapsed_ms = static_cast<int32_t>(now_ms - last_ms_);
        if (elapsed_ms <= 0) return;
        last_ms_ = now_ms;
        fly(elapsed_ms);
        finish_processing();
        if (restarting()) {
            report_ms_ = now_ms;
            return;
        }
        if (now_ms - report_ms_ < solution_period_ms) return;
        report_ms_ = now_ms;
        emit_burst();
    }

   private:
    static constexpr int kCommandBytes = 64;
    static constexpr int kTypeBytes = 7;
    static constexpr int kArgumentsAt = 9;

    char command_[kCommandBytes]{};
    int command_length_{0};
    uint32_t last_tick_ms_{0};
    uint32_t restart_since_ms_{0};
    bool restarting_{false};
    uint16_t processing_id_{0};
    uint32_t processing_since_ms_{0};
    parts::PairResult processing_result_{parts::PairResult::Success};
    uint32_t solutions_{0};
    std::string heard_;
    std::string pending_;
    uint32_t last_ms_{0};
    uint32_t report_ms_{0};
    bool armed_{false};

    static uint32_t argument(const char* text, int at) {
        uint32_t value = 0;
        for (int i = at; text[i] >= '0' && text[i] <= '9'; i++)
            value = value * 10 + static_cast<uint32_t>(text[i] - '0');
        return value;
    }
    static int next_field(const char* text, int at) {
        while (text[at] != 0 && text[at] != ',' && text[at] != '*') at++;
        return text[at] == ',' ? at + 1 : -1;
    }

    bool restarting() const {
        return restarting_ && last_tick_ms_ - restart_since_ms_ < restart_ms;
    }

    void take_command() {
        if (!answers || restarting()) return;
        command_[command_length_] = 0;
        commands_seen++;
        const uint16_t id = parts::pair_command_id(command_);
        if (id == 0) return;
        heard_.append(command_ + 1, kTypeBytes);
        heard_ += ',';
        if (!gnss::nmea_checksum_ok(command_, command_length_)) {
            commands_rejected++;
            acknowledge(id, parts::PairResult::ParameterError);
            return;
        }
        if (id == unsupported_command) {
            acknowledge(id, parts::PairResult::Unsupported);
            return;
        }
        const parts::PairResult result = apply(id, command_ + kArgumentsAt);
        if (id != processing_command) {
            acknowledge(id, result);
            return;
        }
        acknowledge(id, parts::PairResult::Processing);
        processing_id_ = id;
        processing_since_ms_ = last_tick_ms_;
        processing_result_ = result;
    }

    parts::PairResult apply(uint16_t id, const char* arguments);
    void apply_constellations(const char* arguments);
    parts::PairResult apply_sentence_rate(const char* arguments);
    void restart(int kind);
    void acknowledge(uint16_t id, parts::PairResult result);
    void finish_processing() {
        if (processing_id_ == 0 || last_tick_ms_ - processing_since_ms_ < processing_ms) return;
        acknowledge(processing_id_, processing_result_);
        processing_id_ = 0;
    }
    void emit_setting();
    void emit_burst();
    void emit_gga();
    void emit_gll();
    void emit_gsa();
    void emit_gsa_for(uint8_t system_id, uint8_t first_id, uint8_t used);
    void emit_rmc();
    void emit_vtg();
    void emit_zda();
    int put_stamp(char* out) const;
    void finish(char* line, int written);
};

}  // namespace skyblip::models

#include "hardware/parts/ag3335/model_sentences.h"

#endif
