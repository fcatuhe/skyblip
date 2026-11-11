#ifndef SKYBLIP_HARDWARE_PARTS_SPA06_SPA06_H
#define SKYBLIP_HARDWARE_PARTS_SPA06_SPA06_H

#include <cstddef>
#include <cstdint>

#include "core/util/result.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

class Spa06 {
   public:
    // TODO: fc 11nov26 DS 4.3.1, X1 at 0x77 per SoftRF d091924 nRF52.h:151, unverified until #169
    static constexpr uint8_t kAddressPrimary = 0x77;
    static constexpr uint8_t kAddressAlternate = 0x76;
    static constexpr uint8_t kChipId = 0x11;

    // TODO: fc 11nov26 DS Ver2.0 table 8 typical timings, unverified until #169 benches a unit
    static constexpr uint32_t kPressureMeasurementUs = 104400;
    static constexpr uint32_t kTemperatureMeasurementUs = 3600;
    static constexpr uint32_t kConversionMidpointMs = kPressureMeasurementUs / 2 / 1000;

    struct Reading {
        uint32_t pressure_mpa{0};
        int16_t temperature_dc{0};
    };

    explicit Spa06(io::I2c& bus) : bus_(bus) {}

    bool answers() const { return identifies(kAddressPrimary) || identifies(kAddressAlternate); }
    Status begin();
    bool trigger();
    bool read(Reading& out);
    uint8_t address() const { return address_; }

   private:
    // TODO: fc 11nov26 DS Ver2.0 registers, codes and table 4, unverified until #169 benches a unit
    static constexpr uint8_t kRegisterPressure = 0x00;
    static constexpr uint8_t kRegisterTemperature = 0x03;
    static constexpr uint8_t kRegisterPressureConfig = 0x06;
    static constexpr uint8_t kRegisterTemperatureConfig = 0x07;
    static constexpr uint8_t kRegisterMeasurementConfig = 0x08;
    static constexpr uint8_t kRegisterConfig = 0x09;
    static constexpr uint8_t kRegisterChipId = 0x0D;
    static constexpr uint8_t kRegisterCoefficients = 0x10;
    static constexpr size_t kCoefficientBytes = 21;
    static constexpr size_t kResultBytes = 3;
    static constexpr size_t kSettingsBytes = 4;

    static constexpr uint8_t kCoefficientsReady = 0x80;
    static constexpr uint8_t kSensorReady = 0x40;
    static constexpr uint8_t kTemperatureReady = 0x20;
    static constexpr uint8_t kPressureReady = 0x10;
    static constexpr uint8_t kCommandIdle = 0b000;
    static constexpr uint8_t kCommandPressure = 0b001;
    static constexpr uint8_t kCommandTemperature = 0b010;

    static constexpr uint8_t kOversamplingSingle = 0b0000;
    static constexpr uint8_t kOversamplingX64 = 0b0110;
    static constexpr uint8_t kPressureConfig = kOversamplingX64;
    static constexpr uint8_t kTemperatureConfig = kOversamplingSingle;
    static constexpr uint8_t kConfigPressureShift = 0x04;
    static constexpr int64_t kPressureScale = 1040384;
    static constexpr int64_t kTemperatureScale = 524288;

    static constexpr int64_t kFixedPointOne = 65536;
    static constexpr int64_t kPlausiblePressureFloorMpa = int64_t{1000} * 1000;
    static constexpr int64_t kPlausiblePressureCeilingMpa = int64_t{200000} * 1000;
    static constexpr int64_t kPlausibleTemperatureFloorDeciCelsius = -400;
    static constexpr int64_t kPlausibleTemperatureCeilingDeciCelsius = 850;

    enum class Phase : uint8_t { Idle, Pressure, Temperature };

    struct Coefficients {
        int32_t temperature_offset;
        int32_t temperature_slope;
        int32_t pressure_terms[5];
        int32_t temperature_terms[4];
    };

    bool identifies(uint8_t address) const;
    bool read_registers(uint8_t address, uint8_t register_address, uint8_t* out,
                        size_t bytes) const;
    bool write_register(uint8_t register_address, uint8_t value);
    bool forget_configuration();
    bool compensate(int32_t raw_pressure, int32_t raw_temperature, Reading& out) const;
    int64_t pressure_q16(int32_t raw_pressure, int32_t raw_temperature) const;

    io::I2c& bus_;
    Coefficients coefficients_{};
    int32_t raw_pressure_{0};
    uint8_t address_{0};
    bool ready_{false};
    Phase phase_{Phase::Idle};
};

}  // namespace skyblip::parts

#endif
