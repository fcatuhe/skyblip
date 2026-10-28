#include "hardware/parts/bme280/bme280.h"

#include "core/util/intmath.h"
#include "core/util/little_endian.h"

namespace skyblip::parts {

namespace {

int32_t raw_word(const uint8_t* bytes) { return bytes[0] << 12 | bytes[1] << 4 | bytes[2] >> 4; }

}  // namespace

Status Bme280::begin() {
    ready_ = false;
    converting_ = false;
    address_ = identifies(kAddressPrimary)     ? kAddressPrimary
               : identifies(kAddressAlternate) ? kAddressAlternate
                                               : 0;
    if (address_ == 0) return Status::Down;

    // INFO: fc 03oct26 DS 5.4.6: config writes in normal mode may be ignored, so sleep first
    if (!write_register(kRegisterControlMeasurement, kModeSleep)) return Status::Down;
    uint8_t raw[kCalibrationBytes] = {};
    if (!read_registers(address_, kRegisterCalibration, raw, sizeof(raw))) return Status::Down;
    trim_ =
        Trim{get_uint16(raw),     get_int16(raw + 2),  get_int16(raw + 4),  get_uint16(raw + 6),
             get_int16(raw + 8),  get_int16(raw + 10), get_int16(raw + 12), get_int16(raw + 14),
             get_int16(raw + 16), get_int16(raw + 18), get_int16(raw + 20), get_int16(raw + 22)};

    if (!write_register(kRegisterControlHumidity, kOversamplingSkipped)) return Status::Down;
    if (!write_register(kRegisterConfig, kConfigFilter4)) return Status::Down;
    ready_ = true;
    return Status::Ok;
}

bool Bme280::trigger() {
    if (!ready_ && begin() != Status::Ok) return false;
    converting_ = write_register(kRegisterControlMeasurement, kControlMeasurementForced);
    ready_ = converting_;
    return converting_;
}

// INFO: fc 03oct26 config read back with status: a reset part converts on, but unfiltered
bool Bme280::read(Reading& out) {
    if (!converting_) return false;
    uint8_t status[kStatusBytes] = {};
    if (!read_registers(address_, kRegisterStatus, status, sizeof(status)) ||
        status[2] != kConfigFilter4)
        return forget_configuration();
    if ((status[0] & kStatusBusy) != 0) return false;
    converting_ = false;

    uint8_t raw[kResultBytes] = {};
    if (!read_registers(address_, kRegisterResult, raw, sizeof(raw))) return forget_configuration();
    const int32_t raw_pressure = raw_word(raw);
    const int32_t raw_temperature = raw_word(raw + 3);
    if (raw_pressure == kSkippedResult || raw_temperature == kSkippedResult)
        return forget_configuration();
    return compensate(raw_pressure, raw_temperature, out);
}

bool Bme280::forget_configuration() {
    ready_ = false;
    converting_ = false;
    return false;
}

bool Bme280::identifies(uint8_t address) const {
    uint8_t chip_id = 0;
    return read_registers(address, kRegisterChipId, &chip_id, 1) && chip_id == kChipId;
}

bool Bme280::read_registers(uint8_t address, uint8_t register_address, uint8_t* out,
                            size_t bytes) const {
    return bus_.write(address, &register_address, 1) && bus_.read(address, out, bytes);
}

bool Bme280::write_register(uint8_t register_address, uint8_t value) {
    const uint8_t pair[2] = {register_address, value};
    return bus_.write(address_, pair, sizeof(pair));
}

bool Bme280::compensate(int32_t raw_pressure, int32_t raw_temperature, Reading& out) const {
    const int32_t fine_temperature = fine_temperature_from(raw_temperature);
    const int32_t centi_celsius = (fine_temperature * 5 + 128) >> 8;
    const int32_t temperature_dc = divide_rounded<int32_t>(centi_celsius, 10);
    const int64_t pressure_mpa = divide_rounded<int64_t>(
        int64_t{pressure_q24_8(raw_pressure, fine_temperature)} * 1000, 256);
    if (temperature_dc < kPlausibleTemperatureFloorDeciCelsius ||
        temperature_dc > kPlausibleTemperatureCeilingDeciCelsius)
        return false;
    if (pressure_mpa < kPlausiblePressureFloorMpa || pressure_mpa > kPlausiblePressureCeilingMpa)
        return false;
    out.pressure_mpa = static_cast<uint32_t>(pressure_mpa);
    out.temperature_dc = static_cast<int16_t>(temperature_dc);
    return true;
}

int32_t Bme280::fine_temperature_from(int32_t raw_temperature) const {
    const int64_t temperature_1 = trim_.temperature_1;
    const int64_t first_term =
        (((raw_temperature >> 3) - temperature_1 * 2) * trim_.temperature_2) >> 11;
    const int64_t delta = (raw_temperature >> 4) - temperature_1;
    const int64_t second_term = (((delta * delta) >> 12) * trim_.temperature_3) >> 14;
    return static_cast<int32_t>(first_term + second_term);
}

// INFO: fc 03oct26 DS 4.2.3 rev 1.1 with each << on a signed term written as a multiply
uint32_t Bme280::pressure_q24_8(int32_t raw_pressure, int32_t fine_temperature) const {
    int64_t first_term = int64_t{fine_temperature} - 128000;
    int64_t second_term = first_term * first_term * trim_.pressure_6;
    second_term = second_term + first_term * trim_.pressure_5 * (int64_t{1} << 17);
    second_term = second_term + int64_t{trim_.pressure_4} * (int64_t{1} << 35);
    first_term = ((first_term * first_term * trim_.pressure_3) >> 8) +
                 first_term * trim_.pressure_2 * (int64_t{1} << 12);
    first_term = (((int64_t{1} << 47) + first_term) * trim_.pressure_1) >> 33;
    if (first_term == 0) return 0;
    int64_t pressure = 1048576 - raw_pressure;
    pressure = ((pressure * (int64_t{1} << 31)) - second_term) * 3125 / first_term;
    first_term = (int64_t{trim_.pressure_9} * (pressure >> 13) * (pressure >> 13)) >> 25;
    second_term = (int64_t{trim_.pressure_8} * pressure) >> 19;
    pressure = ((pressure + first_term + second_term) >> 8) + int64_t{trim_.pressure_7} * 16;
    return static_cast<uint32_t>(pressure);
}

}  // namespace skyblip::parts
