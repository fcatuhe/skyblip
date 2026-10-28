#ifndef SKYBLIP_HARDWARE_PARTS_BME280_BME280_H
#define SKYBLIP_HARDWARE_PARTS_BME280_BME280_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

class Bme280 {
   public:
    static constexpr uint8_t kAddressPrimary = 0x76;
    static constexpr uint8_t kAddressAlternate = 0x77;
    static constexpr uint8_t kChipId = 0x60;

    static constexpr uint32_t kTemperatureSamples = 2;
    static constexpr uint32_t kPressureSamples = 16;
    static constexpr uint32_t kTemperaturePhaseUs = 1000 + 2000 * kTemperatureSamples;
    static constexpr uint32_t kPressurePhaseUs = 2000 * kPressureSamples + 500;
    static constexpr uint32_t kMeasurementUs = kTemperaturePhaseUs + kPressurePhaseUs;
    static constexpr uint32_t kConversionMidpointMs =
        (kTemperaturePhaseUs + kPressurePhaseUs / 2) / 1000;

    struct Reading {
        uint32_t pressure_mpa{0};
        int16_t temperature_dc{0};
    };

    explicit Bme280(io::I2c& bus) : bus_(bus) {}

    bool answers() const { return identifies(kAddressPrimary) || identifies(kAddressAlternate); }
    Status begin();
    bool trigger();
    bool read(Reading& out);
    uint8_t address() const { return address_; }

   private:
    static constexpr uint8_t kRegisterCalibration = 0x88;
    static constexpr uint8_t kRegisterChipId = 0xD0;
    static constexpr uint8_t kRegisterControlHumidity = 0xF2;
    static constexpr uint8_t kRegisterStatus = 0xF3;
    static constexpr uint8_t kRegisterControlMeasurement = 0xF4;
    static constexpr uint8_t kRegisterConfig = 0xF5;
    static constexpr uint8_t kRegisterResult = 0xF7;
    static constexpr size_t kCalibrationBytes = 24;
    static constexpr size_t kStatusBytes = 3;
    static constexpr size_t kResultBytes = 6;
    static constexpr int32_t kSkippedResult = 0x80000;
    static constexpr uint8_t kStatusBusy = 0x09;

    static constexpr uint8_t kModeSleep = 0b00;
    static constexpr uint8_t kModeForced = 0b01;
    static constexpr uint8_t kOversamplingSkipped = 0b000;
    static constexpr uint8_t kOversamplingX2 = 0b010;
    static constexpr uint8_t kOversamplingX16 = 0b101;
    static constexpr uint8_t kFilter4 = 0b010;
    static constexpr uint8_t kControlMeasurementForced =
        kOversamplingX2 << 5 | kOversamplingX16 << 2 | kModeForced;
    static constexpr uint8_t kConfigFilter4 = kFilter4 << 2;

    static constexpr int64_t kPlausiblePressureFloorMpa = int64_t{1000} * 1000;
    static constexpr int64_t kPlausiblePressureCeilingMpa = int64_t{200000} * 1000;
    static constexpr int32_t kPlausibleTemperatureFloorDeciCelsius = -400;
    static constexpr int32_t kPlausibleTemperatureCeilingDeciCelsius = 850;

    struct Trim {
        uint16_t temperature_1;
        int16_t temperature_2, temperature_3;
        uint16_t pressure_1;
        int16_t pressure_2, pressure_3, pressure_4, pressure_5, pressure_6, pressure_7, pressure_8,
            pressure_9;
    };

    bool identifies(uint8_t address) const;
    bool read_registers(uint8_t address, uint8_t register_address, uint8_t* out,
                        size_t bytes) const;
    bool write_register(uint8_t register_address, uint8_t value);
    bool forget_configuration();
    bool compensate(int32_t raw_pressure, int32_t raw_temperature, Reading& out) const;
    int32_t fine_temperature_from(int32_t raw_temperature) const;
    uint32_t pressure_q24_8(int32_t raw_pressure, int32_t fine_temperature) const;

    io::I2c& bus_;
    Trim trim_{};
    uint8_t address_{0};
    bool ready_{false};
    bool converting_{false};
};

}  // namespace skyblip::parts

#endif
