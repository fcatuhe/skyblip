#ifndef SKYBLIP_HARDWARE_MODEL_BME280_H
#define SKYBLIP_HARDWARE_MODEL_BME280_H

#include <cstdint>

#include "core/flight/atmosphere.h"
#include "hardware/io/io.h"
#include "ports/clock.h"

namespace skyblip::models {

// INFO: fc 03oct26 raw words come from DS 8.1 double maths, not the driver's DS 4.2.3 integers
class Bme280 : public io::I2c {
   public:
    static constexpr uint8_t kAddress = 0x76;
    static constexpr uint8_t kChipId = 0x60;
    static constexpr uint8_t kRegCalibration = 0x88;
    static constexpr uint8_t kRegChipId = 0xD0;
    static constexpr uint8_t kRegReset = 0xE0;
    static constexpr uint8_t kRegCtrlHum = 0xF2;
    static constexpr uint8_t kRegStatus = 0xF3;
    static constexpr uint8_t kRegCtrlMeas = 0xF4;
    static constexpr uint8_t kRegConfig = 0xF5;
    static constexpr uint8_t kRegResult = 0xF7;
    static constexpr uint8_t kResetWord = 0xB6;
    static constexpr int32_t kSkippedResult = 0x80000;
    static constexpr uint8_t kStatusMeasuring = 0x08;

    static constexpr uint16_t kT1 = 27504;
    static constexpr int16_t kT2 = 26435;
    static constexpr int16_t kT3 = -1000;
    static constexpr uint16_t kP1 = 36477;
    static constexpr int16_t kP2 = -10685;
    static constexpr int16_t kP3 = 3024;
    static constexpr int16_t kP4 = 2855;
    static constexpr int16_t kP5 = 140;
    static constexpr int16_t kP6 = -7;
    static constexpr int16_t kP7 = 15500;
    static constexpr int16_t kP8 = -14600;
    static constexpr int16_t kP9 = 6000;

    Bme280() { power_on_reset(); }

    void attach_clock(const ports::Clock& clock) { clock_ = &clock; }

    void set_altitude_mm(int32_t mm) { pressure_mpa_ = flight::alt_mm_to_pressure_mpa(mm); }
    void set_pressure_mpa(uint32_t mpa) { pressure_mpa_ = mpa; }
    uint32_t pressure_mpa() const { return pressure_mpa_; }
    void set_temperature_decicelsius(int16_t deci) { temperature_decicelsius_ = deci; }
    int16_t temperature_decicelsius() const { return temperature_decicelsius_; }

    bool write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (addr != address || !answers) return false;
        writes++;
        if (len == 0) return true;
        pointer = data[0];
        for (size_t i = 0; i + 1 < len; i += 2) write_register(data[i], data[i + 1]);
        return true;
    }

    bool read(uint8_t addr, uint8_t* data, size_t len) override {
        if (addr != address || !answers) return false;
        reads++;
        if (pointer == kRegResult) result_reads++;
        latch_result();
        registers[kRegStatus] = measuring() ? kStatusMeasuring : 0;
        for (size_t i = 0; i < len; i++) data[i] = registers[static_cast<uint8_t>(pointer + i)];
        return true;
    }

    void power_on_reset() {
        for (uint8_t& r : registers) r = 0;
        registers[kRegChipId] = chip_id;
        store_calibration();
        store_word(kRegResult, kSkippedResult);
        store_word(kRegResult + 3, kSkippedResult);
        converting = false;
    }

    bool measuring() const { return converting && now_us() < done_us_; }

    uint8_t mode() const { return registers[kRegCtrlMeas] & 0x03; }
    int pressure_oversampling() const { return oversampling(registers[kRegCtrlMeas] >> 2); }
    int temperature_oversampling() const { return oversampling(registers[kRegCtrlMeas] >> 5); }
    int humidity_oversampling() const { return oversampling(registers[kRegCtrlHum]); }
    int filter_coefficient() const {
        const int code = (registers[kRegConfig] >> 2) & 0x07;
        return code == 0 ? 1 : (code >= 4 ? 16 : 1 << code);
    }
    uint8_t standby_code() const { return registers[kRegConfig] >> 5; }

    uint8_t registers[256]{};
    uint8_t pointer{0};
    uint8_t address{kAddress};
    uint8_t chip_id{kChipId};
    bool answers{true};
    bool converting{false};
    uint32_t extra_measure_us{0};
    int writes{0};
    int reads{0};
    int result_reads{0};
    int ctrl_meas_writes{0};
    int conversions{0};

   private:
    static constexpr int32_t kAdcTop = (1 << 20) - 1;

    static int oversampling(int code) {
        const int c = code & 0x07;
        return c == 0 ? 0 : (c >= 5 ? 16 : 1 << (c - 1));
    }

    void write_register(uint8_t reg, uint8_t value) {
        if (reg == kRegReset) {
            if (value == kResetWord) power_on_reset();
            return;
        }
        if (reg != kRegCtrlHum && reg != kRegCtrlMeas && reg != kRegConfig) return;
        registers[reg] = value;
        if (reg != kRegCtrlMeas) return;
        ctrl_meas_writes++;
        if ((value & 0x03) == 0) return;
        start_conversion();
        registers[kRegCtrlMeas] = static_cast<uint8_t>(value & ~0x03);
    }

    void start_conversion() {
        const int t = temperature_oversampling();
        const int p = pressure_oversampling();
        const uint32_t measure_us =
            1000 + 2000 * t + (p == 0 ? 0 : 2000 * p + 500) + extra_measure_us;
        const int32_t adc_t = temperature_adc();
        pending_t_ = t == 0 ? kSkippedResult : adc_t;
        pending_p_ = p == 0 ? kSkippedResult : pressure_adc(fine(adc_t));
        done_us_ = now_us() + measure_us;
        converting = true;
        conversions++;
    }

    void latch_result() {
        if (!converting || measuring()) return;
        store_word(kRegResult + 3, pending_t_);
        store_word(kRegResult, pending_p_);
    }

    uint64_t now_us() const { return clock_ ? clock_->micros() : 0; }

    void store_word(int reg, int32_t adc) {
        registers[reg] = static_cast<uint8_t>(adc >> 12);
        registers[reg + 1] = static_cast<uint8_t>(adc >> 4);
        registers[reg + 2] = static_cast<uint8_t>((adc & 0x0F) << 4);
    }

    void store_calibration() {
        const uint16_t words[] = {kT1,
                                  static_cast<uint16_t>(kT2),
                                  static_cast<uint16_t>(kT3),
                                  kP1,
                                  static_cast<uint16_t>(kP2),
                                  static_cast<uint16_t>(kP3),
                                  static_cast<uint16_t>(kP4),
                                  static_cast<uint16_t>(kP5),
                                  static_cast<uint16_t>(kP6),
                                  static_cast<uint16_t>(kP7),
                                  static_cast<uint16_t>(kP8),
                                  static_cast<uint16_t>(kP9)};
        for (int i = 0; i < 12; i++) {
            registers[kRegCalibration + 2 * i] = static_cast<uint8_t>(words[i] & 0xFF);
            registers[kRegCalibration + 2 * i + 1] = static_cast<uint8_t>(words[i] >> 8);
        }
    }

    static double fine_unrounded(int32_t adc_t) {
        const double var1 = (adc_t / 16384.0 - kT1 / 1024.0) * kT2;
        const double x = adc_t / 131072.0 - kT1 / 8192.0;
        return var1 + x * x * kT3;
    }

    static int32_t fine(int32_t adc_t) { return static_cast<int32_t>(fine_unrounded(adc_t)); }

    static double pascals(int32_t adc_p, int32_t t_fine) {
        double var1 = t_fine / 2.0 - 64000.0;
        double var2 = var1 * var1 * kP6 / 32768.0;
        var2 = var2 + var1 * kP5 * 2.0;
        var2 = var2 / 4.0 + kP4 * 65536.0;
        var1 = (kP3 * var1 * var1 / 524288.0 + kP2 * var1) / 524288.0;
        var1 = (1.0 + var1 / 32768.0) * kP1;
        double p = 1048576.0 - adc_p;
        p = (p - var2 / 4096.0) * 6250.0 / var1;
        var1 = kP9 * p * p / 2147483648.0;
        var2 = p * kP8 / 32768.0;
        return p + (var1 + var2 + kP7) / 16.0;
    }

    int32_t temperature_adc() const {
        const double want = temperature_decicelsius_ / 10.0 * 5120.0;
        int32_t lo = 0;
        int32_t hi = kAdcTop;
        while (lo < hi) {
            const int32_t mid = lo + (hi - lo) / 2;
            if (fine_unrounded(mid) < want)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    int32_t pressure_adc(int32_t t_fine) const {
        const double want = pressure_mpa_ / 1000.0;
        int32_t lo = 0;
        int32_t hi = kAdcTop;
        while (lo < hi) {
            const int32_t mid = lo + (hi - lo) / 2;
            if (pascals(mid, t_fine) > want)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo > 0 && pascals(lo - 1, t_fine) - want < want - pascals(lo, t_fine)) return lo - 1;
        return lo;
    }

    const ports::Clock* clock_{nullptr};
    uint64_t done_us_{0};
    int32_t pending_t_{kSkippedResult};
    int32_t pending_p_{kSkippedResult};
    uint32_t pressure_mpa_{flight::kIsaSeaLevelPa * 1000};
    int16_t temperature_decicelsius_{flight::kIsaSeaLevelDeciCelsius};
};

}  // namespace skyblip::models

#endif
