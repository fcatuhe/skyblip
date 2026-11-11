#ifndef SKYBLIP_HARDWARE_PARTS_AG3335_PAIR_H
#define SKYBLIP_HARDWARE_PARTS_AG3335_PAIR_H

#include <cstdint>

namespace skyblip::parts {

// INFO: fc 11nov26 $PAIR001 results, SIMCom SIM65M NMEA Message User Guide V1.00 §2.3.1
enum class PairResult : uint8_t {
    Success = 0,
    Processing = 1,
    SendFailed = 2,
    Unsupported = 3,
    ParameterError = 4,
    Busy = 5,
};

uint16_t pair_command_id(const char* sentence);

class PairReader {
   public:
    enum class Heard : uint8_t { Nothing, Ack, Setting };

    Heard feed(char character);

    uint16_t ack_command() const { return ack_command_; }
    PairResult ack_result() const { return ack_result_; }

    const char* firmware_version() const { return firmware_version_; }
    bool identified() const { return firmware_version_[0] != 0; }
    bool dual_band() const { return dual_band_; }

   private:
    // INFO: fc 11nov26 SIM65M §2.3.12 prints a 153-byte $PAIR021 with both L5 fields empty
    static constexpr int kLineBytes = 256;
    static constexpr int kVersionBytes = 32;

    Heard parse(int bytes);
    Heard parse_ack(const char* fields);
    Heard parse_setting(const char* fields);

    char line_[kLineBytes]{};
    int line_bytes_{0};
    char firmware_version_[kVersionBytes]{};
    uint16_t ack_command_{0};
    PairResult ack_result_{PairResult::Success};
    bool dual_band_{false};
};

}  // namespace skyblip::parts

#endif
