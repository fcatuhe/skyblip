#ifndef SKYBLIP_HARDWARE_MODEL_SPA06_H
#define SKYBLIP_HARDWARE_MODEL_SPA06_H

#include <cmath>
#include <cstdint>

#include "core/flight/atmosphere.h"
#include "hardware/io/io.h"
#include "ports/clock.h"

namespace skyblip::models {

// INFO: fc 11nov26 raw words come from DS 4.6.1 in double, not the driver's Q16 Horner
class Spa06 : public io::I2c {
   public:
    static constexpr uint8_t kAddress = 0x77;
    static constexpr uint8_t kChipId = 0x11;
    static constexpr uint8_t kRegisterPressure = 0x00;
    static constexpr uint8_t kRegisterTemperature = 0x03;
    static constexpr uint8_t kRegisterPressureConfig = 0x06;
    static constexpr uint8_t kRegisterTemperatureConfig = 0x07;
    static constexpr uint8_t kRegisterMeasurementConfig = 0x08;
    static constexpr uint8_t kRegisterConfig = 0x09;
    static constexpr uint8_t kRegisterChipId = 0x0D;
    static constexpr uint8_t kRegisterCoefficients = 0x10;
    static constexpr uint8_t kCoefficientsReady = 0x80;
    static constexpr uint8_t kSensorReady = 0x40;
    static constexpr uint8_t kTemperatureReady = 0x20;
    static constexpr uint8_t kPressureReady = 0x10;
    static constexpr uint8_t kPressureShift = 0x04;

    struct Coefficients {
        int32_t c0, c1, c00, c10, c20, c30, c40, c01, c11, c21, c31;
    };

    // TODO: fc 11nov26 a fixture, not a unit's set: no dump is published, replace with #169's
    static constexpr Coefficients kFixture{200, -260,  80000, -60000, -10000, -1500,
                                           100, -2500, 1200,  150,    20};

    Spa06() { power_on_reset(); }

    void attach_clock(const ports::Clock& clock) { clock_ = &clock; }

    void set_altitude_mm(int32_t altitude_mm) {
        pressure_mpa_ = flight::altitude_mm_to_pressure_mpa(altitude_mm);
    }
    void set_pressure_mpa(uint32_t pressure_mpa) { pressure_mpa_ = pressure_mpa; }
    void set_temperature_dc(int16_t temperature_dc) { temperature_dc_ = temperature_dc; }

    bool write(uint8_t address, const uint8_t* data, size_t bytes) override {
        if (address != bus_address || !answers) return false;
        writes++;
        if (bytes == 0) return true;
        pointer = data[0];
        for (size_t i = 0; i + 1 < bytes; i += 2) write_register(data[i], data[i + 1]);
        return true;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (address != bus_address || !answers) return false;
        reads++;
        finish_measurement();
        registers[kRegisterMeasurementConfig] = static_cast<uint8_t>(
            (starting ? 0 : kCoefficientsReady | kSensorReady) |
            (temperature_ready ? kTemperatureReady : 0) | (pressure_ready ? kPressureReady : 0) |
            (registers[kRegisterMeasurementConfig] & 0x0F));
        for (size_t i = 0; i < bytes; i++) {
            const uint8_t at = static_cast<uint8_t>(pointer + i);
            out[i] = registers[at];
            if (at <= kRegisterPressure + 2) pressure_ready = false;
            if (at >= kRegisterTemperature && at <= kRegisterTemperature + 2)
                temperature_ready = false;
        }
        return true;
    }

    void power_on_reset() {
        for (uint8_t& byte : registers) byte = 0;
        registers[kRegisterChipId] = chip_id;
        store_coefficients();
        pressure_ready = false;
        temperature_ready = false;
        measuring_ = Measuring::Nothing;
    }

    bool measuring() const { return measuring_ != Measuring::Nothing && now_us() < done_us_; }
    uint8_t measurement_control() const { return registers[kRegisterMeasurementConfig] & 0x07; }
    int pressure_oversampling() const { return 1 << (registers[kRegisterPressureConfig] & 0x07); }
    int temperature_oversampling() const {
        return 1 << (registers[kRegisterTemperatureConfig] & 0x07);
    }
    double pascals(int32_t raw_pressure, int32_t raw_temperature) const {
        const Coefficients& c = coefficients;
        const double p = raw_pressure / scale_factor(kRegisterPressureConfig);
        const double t = raw_temperature / scale_factor(kRegisterTemperatureConfig);
        return c.c00 + c.c10 * p + c.c20 * p * p + c.c30 * p * p * p + c.c40 * p * p * p * p +
               t * (c.c01 + c.c11 * p + c.c21 * p * p + c.c31 * p * p * p);
    }

    bool pressure_shifted() const { return (registers[kRegisterConfig] & kPressureShift) != 0; }
    uint8_t config() const { return registers[kRegisterConfig]; }

    Coefficients coefficients{kFixture};
    uint8_t registers[0x30]{};
    uint8_t pointer{0};
    uint8_t bus_address{kAddress};
    uint8_t chip_id{kChipId};
    bool answers{true};
    bool starting{false};
    bool raw_forced{false};
    int32_t forced_raw_pressure{0};
    int32_t forced_raw_temperature{0};
    bool pressure_ready{false};
    bool temperature_ready{false};
    int writes{0};
    int reads{0};
    int pressure_commands{0};
    int temperature_commands{0};

   private:
    enum class Measuring : uint8_t { Nothing, Pressure, Temperature };

    static constexpr int32_t kRawCeiling = (1 << 23) - 1;
    static constexpr int32_t kRawFloor = -(1 << 23);
    static constexpr double kScaleFactorByOversampling[8] = {524288, 1572864, 3670016, 7864320,
                                                             253952, 516096,  1040384, 2088960};
    // INFO: fc 11nov26 DS table 8 pressure times, also used for temperature: 7.4 gives single only
    static constexpr uint32_t kMeasurementUsByOversampling[8] = {3600,  5200,  8400,   14800,
                                                                 27600, 53200, 104400, 206800};

    void write_register(uint8_t register_address, uint8_t value) {
        switch (register_address) {
            case kRegisterPressureConfig:
            case kRegisterTemperatureConfig:
            case kRegisterConfig: registers[register_address] = value; return;
            case kRegisterMeasurementConfig:
                registers[register_address] = value & 0x0F;
                start(value & 0x07);
                return;
            default: return;
        }
    }

    void start(uint8_t command) {
        if (command == 0b001) {
            pressure_commands++;
            measuring_ = Measuring::Pressure;
            pending_raw_ = raw_forced ? forced_raw_pressure : pressure_adc(temperature_adc());
            done_us_ = now_us() + measurement_us(kRegisterPressureConfig);
        } else if (command == 0b010) {
            temperature_commands++;
            measuring_ = Measuring::Temperature;
            pending_raw_ = raw_forced ? forced_raw_temperature : temperature_adc();
            done_us_ = now_us() + measurement_us(kRegisterTemperatureConfig);
        }
    }

    void finish_measurement() {
        if (measuring_ == Measuring::Nothing || measuring()) return;
        const bool pressure = measuring_ == Measuring::Pressure;
        store_word(pressure ? kRegisterPressure : kRegisterTemperature, pending_raw_);
        (pressure ? pressure_ready : temperature_ready) = true;
        registers[kRegisterMeasurementConfig] &= 0x08;
        measuring_ = Measuring::Nothing;
    }

    uint64_t now_us() const { return clock_ ? clock_->microseconds() : 0; }

    uint32_t measurement_us(uint8_t config_register) const {
        return kMeasurementUsByOversampling[registers[config_register] & 0x07];
    }
    double scale_factor(uint8_t config_register) const {
        return kScaleFactorByOversampling[registers[config_register] & 0x07];
    }

    void store_word(uint8_t register_address, int32_t raw) {
        const uint32_t word = static_cast<uint32_t>(raw) & 0xFFFFFF;
        registers[register_address] = static_cast<uint8_t>(word >> 16);
        registers[register_address + 1] = static_cast<uint8_t>(word >> 8);
        registers[register_address + 2] = static_cast<uint8_t>(word);
    }

    void store_coefficients() {
        const auto field = [](int32_t value, int bits) {
            return static_cast<uint32_t>(value) & ((uint32_t{1} << bits) - 1);
        };
        const Coefficients& c = coefficients;
        uint8_t* out = registers + kRegisterCoefficients;
        out[0] = static_cast<uint8_t>(field(c.c0, 12) >> 4);
        out[1] = static_cast<uint8_t>((field(c.c0, 12) & 0x0F) << 4 | field(c.c1, 12) >> 8);
        out[2] = static_cast<uint8_t>(field(c.c1, 12));
        out[3] = static_cast<uint8_t>(field(c.c00, 20) >> 12);
        out[4] = static_cast<uint8_t>(field(c.c00, 20) >> 4);
        out[5] = static_cast<uint8_t>((field(c.c00, 20) & 0x0F) << 4 | field(c.c10, 20) >> 16);
        out[6] = static_cast<uint8_t>(field(c.c10, 20) >> 8);
        out[7] = static_cast<uint8_t>(field(c.c10, 20));
        const int32_t words[] = {c.c01, c.c11, c.c20, c.c21, c.c30};
        for (int i = 0; i < 5; i++) {
            out[8 + 2 * i] = static_cast<uint8_t>(field(words[i], 16) >> 8);
            out[9 + 2 * i] = static_cast<uint8_t>(field(words[i], 16));
        }
        out[18] = static_cast<uint8_t>(field(c.c31, 12) >> 4);
        out[19] = static_cast<uint8_t>((field(c.c31, 12) & 0x0F) << 4 | field(c.c40, 12) >> 8);
        out[20] = static_cast<uint8_t>(field(c.c40, 12));
    }

    int32_t temperature_adc() const {
        const double scaled = (temperature_dc_ / 10.0 - coefficients.c0 * 0.5) / coefficients.c1;
        const double raw = std::round(scaled * scale_factor(kRegisterTemperatureConfig));
        return static_cast<int32_t>(std::fmax(kRawFloor, std::fmin(kRawCeiling, raw)));
    }

    int32_t pressure_adc(int32_t raw_temperature) const {
        const double want = pressure_mpa_ / 1000.0;
        const bool falling = pascals(kRawCeiling, raw_temperature) < pascals(0, raw_temperature);
        int32_t low = kRawFloor;
        int32_t high = kRawCeiling;
        while (low < high) {
            const int32_t middle = low + (high - low) / 2;
            const double at = pascals(middle, raw_temperature);
            if (falling ? at > want : at < want)
                low = middle + 1;
            else
                high = middle;
        }
        if (low > kRawFloor && std::fabs(pascals(low - 1, raw_temperature) - want) <
                                   std::fabs(pascals(low, raw_temperature) - want))
            return low - 1;
        return low;
    }

    const ports::Clock* clock_{nullptr};
    Measuring measuring_{Measuring::Nothing};
    uint64_t done_us_{0};
    int32_t pending_raw_{0};
    uint32_t pressure_mpa_{flight::kIsaSeaLevelPa * 1000};
    int16_t temperature_dc_{flight::kIsaSeaLevelDeciCelsius};
};

}  // namespace skyblip::models

#endif
