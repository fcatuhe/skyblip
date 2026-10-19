#ifndef SKYBLIP_HARDWARE_MODEL_BME280_H
#define SKYBLIP_HARDWARE_MODEL_BME280_H

#include <cstdint>

#include "core/flight/atmosphere.h"
#include "core/util/little_endian.h"
#include "hardware/io/io.h"
#include "ports/clock.h"

namespace skyblip::models {

// INFO: fc 03oct26 raw words come from DS 8.1 double maths, not the driver's DS 4.2.3 integers
class Bme280 : public io::I2c {
   public:
    static constexpr uint8_t kAddress = 0x76;
    static constexpr uint8_t kChipId = 0x60;
    static constexpr uint8_t kRegisterCalibration = 0x88;
    static constexpr uint8_t kRegisterChipId = 0xD0;
    static constexpr uint8_t kRegisterReset = 0xE0;
    static constexpr uint8_t kRegisterControlHumidity = 0xF2;
    static constexpr uint8_t kRegisterStatus = 0xF3;
    static constexpr uint8_t kRegisterControlMeasurement = 0xF4;
    static constexpr uint8_t kRegisterConfig = 0xF5;
    static constexpr uint8_t kRegisterResult = 0xF7;
    static constexpr uint8_t kResetWord = 0xB6;
    static constexpr int32_t kSkippedResult = 0x80000;
    static constexpr uint8_t kStatusMeasuring = 0x08;

    static constexpr uint16_t kTemperatureTrim1 = 27504;
    static constexpr int16_t kTemperatureTrim2 = 26435;
    static constexpr int16_t kTemperatureTrim3 = -1000;
    static constexpr uint16_t kPressureTrim1 = 36477;
    static constexpr int16_t kPressureTrim2 = -10685;
    static constexpr int16_t kPressureTrim3 = 3024;
    static constexpr int16_t kPressureTrim4 = 2855;
    static constexpr int16_t kPressureTrim5 = 140;
    static constexpr int16_t kPressureTrim6 = -7;
    static constexpr int16_t kPressureTrim7 = 15500;
    static constexpr int16_t kPressureTrim8 = -14600;
    static constexpr int16_t kPressureTrim9 = 6000;

    Bme280() { power_on_reset(); }

    void attach_clock(const ports::Clock& clock) { clock_ = &clock; }

    void set_altitude_mm(int32_t altitude_mm) {
        pressure_mpa_ = flight::altitude_mm_to_pressure_mpa(altitude_mm);
    }
    void set_pressure_mpa(uint32_t pressure_mpa) { pressure_mpa_ = pressure_mpa; }
    uint32_t pressure_mpa() const { return pressure_mpa_; }
    void set_temperature_dc(int16_t temperature_dc) { temperature_dc_ = temperature_dc; }
    int16_t temperature_dc() const { return temperature_dc_; }

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
        if (pointer == kRegisterResult) result_reads++;
        latch_result();
        registers[kRegisterStatus] = measuring() ? kStatusMeasuring : 0;
        for (size_t i = 0; i < bytes; i++) out[i] = registers[static_cast<uint8_t>(pointer + i)];
        return true;
    }

    void power_on_reset() {
        for (uint8_t& byte : registers) byte = 0;
        registers[kRegisterChipId] = chip_id;
        store_calibration();
        store_word(kRegisterResult, kSkippedResult);
        store_word(kRegisterResult + 3, kSkippedResult);
        converting = false;
    }

    bool measuring() const { return converting && now_us() < done_us_; }

    uint8_t mode() const { return registers[kRegisterControlMeasurement] & 0x03; }
    int pressure_oversampling() const {
        return oversampling(registers[kRegisterControlMeasurement] >> 2);
    }
    int temperature_oversampling() const {
        return oversampling(registers[kRegisterControlMeasurement] >> 5);
    }
    int humidity_oversampling() const { return oversampling(registers[kRegisterControlHumidity]); }
    int filter_coefficient() const {
        const int code = (registers[kRegisterConfig] >> 2) & 0x07;
        return code == 0 ? 1 : (code >= 4 ? 16 : 1 << code);
    }
    uint8_t standby_code() const { return registers[kRegisterConfig] >> 5; }

    uint8_t registers[256]{};
    uint8_t pointer{0};
    uint8_t bus_address{kAddress};
    uint8_t chip_id{kChipId};
    bool answers{true};
    bool converting{false};
    uint32_t extra_measurement_us{0};
    int writes{0};
    int reads{0};
    int result_reads{0};
    int control_measurement_writes{0};
    int conversions{0};

   private:
    static constexpr int32_t kAdcTop = (1 << 20) - 1;

    static int oversampling(int code) {
        const int field = code & 0x07;
        return field == 0 ? 0 : (field >= 5 ? 16 : 1 << (field - 1));
    }

    void write_register(uint8_t register_address, uint8_t value) {
        if (register_address == kRegisterReset) {
            if (value == kResetWord) power_on_reset();
            return;
        }
        if (register_address != kRegisterControlHumidity &&
            register_address != kRegisterControlMeasurement && register_address != kRegisterConfig)
            return;
        registers[register_address] = value;
        if (register_address != kRegisterControlMeasurement) return;
        control_measurement_writes++;
        if ((value & 0x03) == 0) return;
        start_conversion();
        registers[kRegisterControlMeasurement] = static_cast<uint8_t>(value & ~0x03);
    }

    void start_conversion() {
        const int temperature_samples = temperature_oversampling();
        const int pressure_samples = pressure_oversampling();
        const uint32_t measurement_us =
            1000 + 2000 * temperature_samples +
            (pressure_samples == 0 ? 0 : 2000 * pressure_samples + 500) + extra_measurement_us;
        const int32_t raw_temperature = temperature_adc();
        pending_raw_temperature_ = temperature_samples == 0 ? kSkippedResult : raw_temperature;
        pending_raw_pressure_ = pressure_samples == 0
                                    ? kSkippedResult
                                    : pressure_adc(fine_temperature_from(raw_temperature));
        done_us_ = now_us() + measurement_us;
        converting = true;
        conversions++;
    }

    void latch_result() {
        if (!converting || measuring()) return;
        store_word(kRegisterResult + 3, pending_raw_temperature_);
        store_word(kRegisterResult, pending_raw_pressure_);
    }

    uint64_t now_us() const { return clock_ ? clock_->micros() : 0; }

    void store_word(int register_address, int32_t raw) {
        registers[register_address] = static_cast<uint8_t>(raw >> 12);
        registers[register_address + 1] = static_cast<uint8_t>(raw >> 4);
        registers[register_address + 2] = static_cast<uint8_t>((raw & 0x0F) << 4);
    }

    void store_calibration() {
        const uint16_t words[] = {kTemperatureTrim1,
                                  static_cast<uint16_t>(kTemperatureTrim2),
                                  static_cast<uint16_t>(kTemperatureTrim3),
                                  kPressureTrim1,
                                  static_cast<uint16_t>(kPressureTrim2),
                                  static_cast<uint16_t>(kPressureTrim3),
                                  static_cast<uint16_t>(kPressureTrim4),
                                  static_cast<uint16_t>(kPressureTrim5),
                                  static_cast<uint16_t>(kPressureTrim6),
                                  static_cast<uint16_t>(kPressureTrim7),
                                  static_cast<uint16_t>(kPressureTrim8),
                                  static_cast<uint16_t>(kPressureTrim9)};
        for (size_t i = 0; i < 12; i++) put_u16(registers + kRegisterCalibration + 2 * i, words[i]);
    }

    static double unrounded_fine_temperature(int32_t raw_temperature) {
        const double first_term =
            (raw_temperature / 16384.0 - kTemperatureTrim1 / 1024.0) * kTemperatureTrim2;
        const double offset = raw_temperature / 131072.0 - kTemperatureTrim1 / 8192.0;
        return first_term + offset * offset * kTemperatureTrim3;
    }

    static int32_t fine_temperature_from(int32_t raw_temperature) {
        return static_cast<int32_t>(unrounded_fine_temperature(raw_temperature));
    }

    static double pascals(int32_t raw_pressure, int32_t fine_temperature) {
        double first_term = fine_temperature / 2.0 - 64000.0;
        double second_term = first_term * first_term * kPressureTrim6 / 32768.0;
        second_term = second_term + first_term * kPressureTrim5 * 2.0;
        second_term = second_term / 4.0 + kPressureTrim4 * 65536.0;
        first_term =
            (kPressureTrim3 * first_term * first_term / 524288.0 + kPressureTrim2 * first_term) /
            524288.0;
        first_term = (1.0 + first_term / 32768.0) * kPressureTrim1;
        double pressure = 1048576.0 - raw_pressure;
        pressure = (pressure - second_term / 4096.0) * 6250.0 / first_term;
        first_term = kPressureTrim9 * pressure * pressure / 2147483648.0;
        second_term = pressure * kPressureTrim8 / 32768.0;
        return pressure + (first_term + second_term + kPressureTrim7) / 16.0;
    }

    int32_t temperature_adc() const {
        const double want = temperature_dc_ / 10.0 * 5120.0;
        int32_t low = 0;
        int32_t high = kAdcTop;
        while (low < high) {
            const int32_t middle = low + (high - low) / 2;
            if (unrounded_fine_temperature(middle) < want)
                low = middle + 1;
            else
                high = middle;
        }
        return low;
    }

    int32_t pressure_adc(int32_t fine_temperature) const {
        const double want = pressure_mpa_ / 1000.0;
        int32_t low = 0;
        int32_t high = kAdcTop;
        while (low < high) {
            const int32_t middle = low + (high - low) / 2;
            if (pascals(middle, fine_temperature) > want)
                low = middle + 1;
            else
                high = middle;
        }
        if (low > 0 &&
            pascals(low - 1, fine_temperature) - want < want - pascals(low, fine_temperature))
            return low - 1;
        return low;
    }

    const ports::Clock* clock_{nullptr};
    uint64_t done_us_{0};
    int32_t pending_raw_temperature_{kSkippedResult};
    int32_t pending_raw_pressure_{kSkippedResult};
    uint32_t pressure_mpa_{flight::kIsaSeaLevelPa * 1000};
    int16_t temperature_dc_{flight::kIsaSeaLevelDeciCelsius};
};

}  // namespace skyblip::models

#endif
