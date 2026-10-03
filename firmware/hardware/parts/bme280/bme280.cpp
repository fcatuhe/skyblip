#include "hardware/parts/bme280/bme280.h"

#include "core/util/intmath.h"

namespace skyblip::parts {

namespace {

uint16_t u16_le(const uint8_t* b) { return static_cast<uint16_t>(b[0] | b[1] << 8); }
int16_t s16_le(const uint8_t* b) { return static_cast<int16_t>(u16_le(b)); }
int32_t adc_word(const uint8_t* b) { return b[0] << 12 | b[1] << 4 | b[2] >> 4; }

}  // namespace

Status Bme280::begin() {
    ready_ = false;
    converting_ = false;
    address_ = identifies(kAddressPrimary)     ? kAddressPrimary
               : identifies(kAddressAlternate) ? kAddressAlternate
                                               : 0;
    if (address_ == 0) return Status::Down;

    // INFO: fc 03oct26 DS 5.4.6: config writes in normal mode may be ignored, so sleep first
    if (!write_register(kRegCtrlMeas, kModeSleep)) return Status::Down;
    uint8_t raw[kCalibrationBytes] = {};
    if (!read_registers(address_, kRegCalibration, raw, sizeof(raw))) return Status::Down;
    trim_ = Trim{u16_le(raw),      s16_le(raw + 2),  s16_le(raw + 4),  u16_le(raw + 6),
                 s16_le(raw + 8),  s16_le(raw + 10), s16_le(raw + 12), s16_le(raw + 14),
                 s16_le(raw + 16), s16_le(raw + 18), s16_le(raw + 20), s16_le(raw + 22)};

    if (!write_register(kRegCtrlHum, kOversamplingSkipped)) return Status::Down;
    if (!write_register(kRegConfig, kConfigFilter4)) return Status::Down;
    ready_ = true;
    return Status::Ok;
}

bool Bme280::trigger() {
    if (!ready_ && begin() != Status::Ok) return false;
    converting_ = write_register(kRegCtrlMeas, kCtrlMeasForced);
    ready_ = converting_;
    return converting_;
}

// INFO: fc 03oct26 config read back with status: a reset part converts on, but unfiltered
bool Bme280::read(Reading& out) {
    if (!converting_) return false;
    uint8_t status[kStatusBytes] = {};
    if (!read_registers(address_, kRegStatus, status, sizeof(status)) ||
        status[2] != kConfigFilter4)
        return forget_configuration();
    if ((status[0] & kStatusBusy) != 0) return false;
    converting_ = false;

    uint8_t raw[kResultBytes] = {};
    if (!read_registers(address_, kRegResult, raw, sizeof(raw))) return forget_configuration();
    const int32_t adc_p = adc_word(raw);
    const int32_t adc_t = adc_word(raw + 3);
    if (adc_p == kSkippedResult || adc_t == kSkippedResult) return forget_configuration();
    return compensate(adc_p, adc_t, out);
}

bool Bme280::forget_configuration() {
    ready_ = false;
    converting_ = false;
    return false;
}

bool Bme280::identifies(uint8_t address) const {
    uint8_t id = 0;
    return read_registers(address, kRegChipId, &id, 1) && id == kChipId;
}

bool Bme280::read_registers(uint8_t address, uint8_t reg, uint8_t* out, size_t len) const {
    return bus_.write(address, &reg, 1) && bus_.read(address, out, len);
}

bool Bme280::write_register(uint8_t reg, uint8_t value) {
    const uint8_t pair[2] = {reg, value};
    return bus_.write(address_, pair, sizeof(pair));
}

bool Bme280::compensate(int32_t adc_p, int32_t adc_t, Reading& out) const {
    const int32_t t_fine = fine_temperature(adc_t);
    const int32_t centi = (t_fine * 5 + 128) >> 8;
    const int32_t deci = div_round<int32_t>(centi, 10);
    const int64_t mpa = div_round<int64_t>(int64_t{pressure_q24_8(adc_p, t_fine)} * 1000, 256);
    if (deci < kMinPlausibleDeciCelsius || deci > kMaxPlausibleDeciCelsius) return false;
    if (mpa < kMinPlausibleMpa || mpa > kMaxPlausibleMpa) return false;
    out.pressure_mpa = static_cast<uint32_t>(mpa);
    out.temperature_decicelsius = static_cast<int16_t>(deci);
    return true;
}

int32_t Bme280::fine_temperature(int32_t adc_t) const {
    const int64_t t1 = trim_.t1;
    const int64_t var1 = (((adc_t >> 3) - t1 * 2) * trim_.t2) >> 11;
    const int64_t delta = (adc_t >> 4) - t1;
    const int64_t var2 = (((delta * delta) >> 12) * trim_.t3) >> 14;
    return static_cast<int32_t>(var1 + var2);
}

// INFO: fc 03oct26 DS 4.2.3 rev 1.1 with each << on a signed term written as a multiply
uint32_t Bme280::pressure_q24_8(int32_t adc_p, int32_t t_fine) const {
    int64_t var1 = int64_t{t_fine} - 128000;
    int64_t var2 = var1 * var1 * trim_.p6;
    var2 = var2 + var1 * trim_.p5 * (int64_t{1} << 17);
    var2 = var2 + int64_t{trim_.p4} * (int64_t{1} << 35);
    var1 = ((var1 * var1 * trim_.p3) >> 8) + var1 * trim_.p2 * (int64_t{1} << 12);
    var1 = (((int64_t{1} << 47) + var1) * trim_.p1) >> 33;
    if (var1 == 0) return 0;
    int64_t p = 1048576 - adc_p;
    p = ((p * (int64_t{1} << 31)) - var2) * 3125 / var1;
    var1 = (int64_t{trim_.p9} * (p >> 13) * (p >> 13)) >> 25;
    var2 = (int64_t{trim_.p8} * p) >> 19;
    p = ((p + var1 + var2) >> 8) + int64_t{trim_.p7} * 16;
    return static_cast<uint32_t>(p);
}

}  // namespace skyblip::parts
