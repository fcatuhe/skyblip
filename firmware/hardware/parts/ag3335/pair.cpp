#include "hardware/parts/ag3335/pair.h"

#include <cstring>

#include "core/gnss/nmea.h"

namespace skyblip::parts {

namespace {

constexpr const char* kTalker = "$PAIR";
constexpr int kTalkerBytes = 5;
constexpr int kIdDigits = 3;
constexpr uint16_t kAckId = 1;
constexpr uint16_t kSettingId = 21;
constexpr uint16_t kDualBandId = 105;

bool digits(const char* text, int count) {
    for (int i = 0; i < count; i++)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

uint16_t three_digits(const char* text) {
    return static_cast<uint16_t>((text[0] - '0') * 100 + (text[1] - '0') * 10 + (text[2] - '0'));
}

}  // namespace

uint16_t pair_command_id(const char* sentence) {
    if (std::strncmp(sentence, kTalker, kTalkerBytes) != 0) return 0;
    if (!digits(sentence + kTalkerBytes, kIdDigits)) return 0;
    return three_digits(sentence + kTalkerBytes);
}

PairReader::Heard PairReader::feed(char character) {
    if (character == '$') {
        line_bytes_ = 0;
        line_[line_bytes_++] = character;
        return Heard::Nothing;
    }
    if (character == '\r' || character == '\n') {
        const int bytes = line_bytes_;
        line_bytes_ = 0;
        return bytes > 0 ? parse(bytes) : Heard::Nothing;
    }
    if (line_bytes_ == 0) return Heard::Nothing;
    if (line_bytes_ < kLineBytes - 1) {
        line_[line_bytes_++] = character;
        return Heard::Nothing;
    }
    line_bytes_ = 0;
    return Heard::Nothing;
}

PairReader::Heard PairReader::parse(int bytes) {
    if (bytes < kTalkerBytes + kIdDigits || std::strncmp(line_, kTalker, kTalkerBytes) != 0)
        return Heard::Nothing;
    if (!gnss::nmea_checksum_ok(line_, bytes)) return Heard::Nothing;
    line_[bytes] = 0;
    const uint16_t id = pair_command_id(line_);
    const char* fields = line_ + kTalkerBytes + kIdDigits;
    if (*fields != ',') return Heard::Nothing;
    fields++;
    if (id == kAckId) return parse_ack(fields);
    if (id == kSettingId) return parse_setting(fields);
    if (id == kDualBandId) return parse_dual_band(fields);
    return Heard::Nothing;
}

PairReader::Heard PairReader::parse_ack(const char* fields) {
    if (!digits(fields, kIdDigits) || fields[kIdDigits] != ',') return Heard::Nothing;
    const char result = fields[kIdDigits + 1];
    if (result < '0' || result > '5') return Heard::Nothing;
    ack_command_ = three_digits(fields);
    ack_result_ = static_cast<PairResult>(result - '0');
    return Heard::Ack;
}

PairReader::Heard PairReader::parse_setting(const char* fields) {
    const char* comma = std::strchr(fields, ',');
    if (comma == nullptr || comma == fields) return Heard::Nothing;
    const int length = static_cast<int>(comma - fields);
    const int kept = length < kVersionBytes - 1 ? length : kVersionBytes - 1;
    std::memcpy(firmware_version_, fields, static_cast<size_t>(kept));
    firmware_version_[kept] = 0;
    const char frequency = comma[1];
    frequency_ = (frequency == 'S' || frequency == 'D') && (comma[2] == ',' || comma[2] == '*')
                     ? frequency
                     : 0;
    return Heard::Setting;
}

// INFO: fc 26nov26 the reply to $PAIR105 is 1 with dual band on and 0 with it off, LC29H V1.4
// §2.4.30
PairReader::Heard PairReader::parse_dual_band(const char* fields) {
    if ((fields[0] != '0' && fields[0] != '1') || fields[1] != '*') return Heard::Nothing;
    dual_band_on_ = fields[0] == '1';
    return Heard::DualBand;
}

}  // namespace skyblip::parts
