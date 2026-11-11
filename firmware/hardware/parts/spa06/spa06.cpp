#include "hardware/parts/spa06/spa06.h"

#include "core/util/intmath.h"

namespace skyblip::parts {

namespace {

int32_t sign_extended(uint32_t field, int bits) {
    const int32_t sign = int32_t{1} << (bits - 1);
    return static_cast<int32_t>(field ^ static_cast<uint32_t>(sign)) - sign;
}

int32_t raw_word(const uint8_t* bytes) {
    return sign_extended(uint32_t{bytes[0]} << 16 | uint32_t{bytes[1]} << 8 | bytes[2], 24);
}

int32_t field16(const uint8_t* bytes) { return sign_extended(bytes[0] << 8 | bytes[1], 16); }

}  // namespace

Status Spa06::begin() {
    ready_ = false;
    phase_ = Phase::Idle;
    address_ = identifies(kAddressPrimary)     ? kAddressPrimary
               : identifies(kAddressAlternate) ? kAddressAlternate
                                               : 0;
    if (address_ == 0) return Status::Down;

    uint8_t status = 0;
    if (!read_registers(address_, kRegisterMeasurementConfig, &status, 1)) return Status::Down;
    if ((status & (kCoefficientsReady | kSensorReady)) != (kCoefficientsReady | kSensorReady))
        return Status::WouldBlock;

    uint8_t raw[kCoefficientBytes] = {};
    if (!read_registers(address_, kRegisterCoefficients, raw, sizeof(raw))) return Status::Down;
    coefficients_ = Coefficients{
        sign_extended(raw[0] << 4 | raw[1] >> 4, 12),
        sign_extended((raw[1] & 0x0F) << 8 | raw[2], 12),
        {sign_extended(raw[3] << 12 | raw[4] << 4 | raw[5] >> 4, 20),
         sign_extended((raw[5] & 0x0F) << 16 | raw[6] << 8 | raw[7], 20), field16(raw + 12),
         field16(raw + 16), sign_extended((raw[19] & 0x0F) << 8 | raw[20], 12)},
        {field16(raw + 8), field16(raw + 10), field16(raw + 14),
         sign_extended(raw[18] << 4 | raw[19] >> 4, 12)}};

    // INFO: fc 11nov26 Adafruit's driver, which SoftRF uses, leaves the part measuring at 200 Hz
    if (!write_register(kRegisterMeasurementConfig, kCommandIdle)) return Status::Down;
    if (!write_register(kRegisterPressureConfig, kPressureConfig)) return Status::Down;
    if (!write_register(kRegisterTemperatureConfig, kTemperatureConfig)) return Status::Down;
    if (!write_register(kRegisterConfig, kConfigPressureShift)) return Status::Down;
    ready_ = true;
    return Status::Ok;
}

// INFO: fc 11nov26 DS 7.5: reading a result clears its ready flag, so the next flag is this one's
bool Spa06::trigger() {
    if (!ready_ && begin() != Status::Ok) return false;
    uint8_t stale[2 * kResultBytes] = {};
    if (!read_registers(address_, kRegisterPressure, stale, sizeof(stale)) ||
        !write_register(kRegisterMeasurementConfig, kCommandPressure))
        return forget_configuration();
    phase_ = Phase::Pressure;
    return true;
}

// INFO: fc 11nov26 config read each poll: a reset part measures at 1x unshifted when next commanded
bool Spa06::read(Reading& out) {
    if (phase_ == Phase::Idle) return false;
    uint8_t settings[kSettingsBytes] = {};
    if (!read_registers(address_, kRegisterPressureConfig, settings, sizeof(settings)) ||
        settings[0] != kPressureConfig || settings[3] != kConfigPressureShift)
        return forget_configuration();
    const uint8_t status = settings[2];

    uint8_t raw[kResultBytes] = {};
    if (phase_ == Phase::Pressure) {
        if ((status & kPressureReady) == 0) return false;
        if (!read_registers(address_, kRegisterPressure, raw, sizeof(raw)) ||
            !write_register(kRegisterMeasurementConfig, kCommandTemperature))
            return forget_configuration();
        raw_pressure_ = raw_word(raw);
        phase_ = Phase::Temperature;
        return false;
    }

    if ((status & kTemperatureReady) == 0) return false;
    if (!read_registers(address_, kRegisterTemperature, raw, sizeof(raw)))
        return forget_configuration();
    phase_ = Phase::Idle;
    return compensate(raw_pressure_, raw_word(raw), out);
}

bool Spa06::forget_configuration() {
    ready_ = false;
    phase_ = Phase::Idle;
    return false;
}

bool Spa06::identifies(uint8_t address) const {
    uint8_t chip_id = 0;
    return read_registers(address, kRegisterChipId, &chip_id, 1) && chip_id == kChipId;
}

bool Spa06::read_registers(uint8_t address, uint8_t register_address, uint8_t* out,
                           size_t bytes) const {
    return bus_.write(address, &register_address, 1) && bus_.read(address, out, bytes);
}

bool Spa06::write_register(uint8_t register_address, uint8_t value) {
    const uint8_t pair[2] = {register_address, value};
    return bus_.write(address_, pair, sizeof(pair));
}

bool Spa06::compensate(int32_t raw_pressure, int32_t raw_temperature, Reading& out) const {
    const int64_t temperature_dc =
        divide_rounded<int64_t>(int64_t{coefficients_.temperature_offset} * 5 * kTemperatureScale +
                                    int64_t{coefficients_.temperature_slope} * raw_temperature * 10,
                                kTemperatureScale);
    const int64_t pressure_mpa =
        divide_rounded<int64_t>(pressure_q16(raw_pressure, raw_temperature) * 1000, kFixedPointOne);
    if (temperature_dc < kPlausibleTemperatureFloorDeciCelsius ||
        temperature_dc > kPlausibleTemperatureCeilingDeciCelsius)
        return false;
    if (pressure_mpa < kPlausiblePressureFloorMpa || pressure_mpa > kPlausiblePressureCeilingMpa)
        return false;
    out.pressure_mpa = static_cast<uint32_t>(pressure_mpa);
    out.temperature_dc = static_cast<int16_t>(temperature_dc);
    return true;
}

// INFO: fc 11nov26 DS 4.6.1 by Horner in Q16, every product under 2^61 for any 24-bit raw word
int64_t Spa06::pressure_q16(int32_t raw_pressure, int32_t raw_temperature) const {
    int64_t pressure = 0;
    for (int i = 4; i >= 0; i--)
        pressure = coefficients_.pressure_terms[i] * kFixedPointOne +
                   divide_rounded<int64_t>(pressure * raw_pressure, kPressureScale);
    int64_t temperature = 0;
    for (int i = 3; i >= 0; i--)
        temperature = coefficients_.temperature_terms[i] * kFixedPointOne +
                      divide_rounded<int64_t>(temperature * raw_pressure, kPressureScale);
    return pressure + divide_rounded<int64_t>(temperature * raw_temperature, kTemperatureScale);
}

}  // namespace skyblip::parts
