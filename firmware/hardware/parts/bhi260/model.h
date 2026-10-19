#ifndef SKYBLIP_HARDWARE_MODEL_BHI260_H
#define SKYBLIP_HARDWARE_MODEL_BHI260_H

#include <cstdint>
#include <cstring>

#include "core/util/little_endian.h"
#include "hardware/io/io.h"

namespace skyblip::models {

class Bhi260 : public io::I2c {
   public:
    static constexpr uint8_t kAddress = 0x28;
    static constexpr uint16_t kFirmwareMagic = 0x662B;
    static constexpr uint16_t kKernelVersion = 0x1234;
    static constexpr int kMaxStreamBytes = 64;

    struct Stream {
        uint8_t bytes[kMaxStreamBytes]{};
        size_t len{0};
        size_t pos{0};
    };

    bool write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (addr != address || !answers) return false;
        if (len == 0) return true;
        pointer = data[0];
        if (pointer == kRegCommand) return accept_command(data + 1, len - 1);
        for (size_t i = 1; i < len; i++) {
            const uint8_t reg = static_cast<uint8_t>(pointer + i - 1);
            if (reg == kRegResetRequest && data[i] != 0) reset();
        }
        return true;
    }

    bool read(uint8_t addr, uint8_t* data, size_t len) override {
        if (addr != address || !answers) return false;
        if (pointer == kRegFifoWakeup) return read_fifo(wakeup_, data, len);
        if (pointer == kRegFifoNonWakeup) return read_fifo(non_wakeup_, data, len);
        if (pointer == kRegStatusChannel) return read_status(data, len);
        for (size_t i = 0; i < len; i++)
            data[i] = register_value(static_cast<uint8_t>(pointer + i));
        return true;
    }

    void set_acceleration(int16_t x, int16_t y, int16_t z) {
        x_mg = x;
        y_mg = y;
        z_mg = z;
    }

    void set_angular_rate(int16_t x, int16_t y, int16_t z) {
        x_cdps = x;
        y_cdps = y;
        z_cdps = z;
    }

    void report_meta_event(uint8_t event, uint8_t first, uint8_t second) {
        meta_event_ = event;
        meta_first_ = first;
        meta_second_ = second;
        meta_pending_ = true;
        meta_pending_wakeup_ = true;
    }

    bool running() const { return booted && sample_rate_hz > 0; }
    bool gyro_streaming() const { return booted && gyro_rate_hz > 0; }
    uint32_t uploaded() const { return uploaded_; }

    uint8_t address{kAddress};
    bool answers{true};
    bool accepts_firmware{true};
    uint8_t product_id{0x89};
    bool announces_itself{true};
    bool accel_present{true};
    bool gyro_present{true};
    bool accepts_configuration{true};
    uint8_t error_value{0};
    bool host_interface_ready{true};
    bool booted{false};
    bool verify_error{false};
    uint8_t accel_sensor_id{0};
    uint16_t accel_range_g{0};
    uint8_t gyro_sensor_id{0};
    uint16_t gyro_range_dps{0};
    float gyro_rate_hz{0};
    float sample_rate_hz{0};
    uint32_t latency_ms{0};
    int16_t x_mg{0};
    int16_t y_mg{1000};
    int16_t z_mg{0};
    int16_t x_cdps{0};
    int16_t y_cdps{0};
    int16_t z_cdps{0};
    int resets{0};

   private:
    static constexpr uint8_t kRegCommand = 0x00;
    static constexpr uint8_t kRegFifoWakeup = 0x01;
    static constexpr uint8_t kRegFifoNonWakeup = 0x02;
    static constexpr uint8_t kRegStatusChannel = 0x03;
    static constexpr uint8_t kRegResetRequest = 0x14;
    static constexpr uint8_t kRegInterruptStatus = 0x2D;
    static constexpr uint8_t kInterruptFifoNonWakeup = 0x18;
    static constexpr uint8_t kInterruptStatusChannel = 0x20;
    static constexpr uint16_t kParamReadMask = 0x1000;
    static constexpr uint16_t kParamSensorsPresent = 0x011F;
    static constexpr uint16_t kParamSensorConfig = 0x0500;
    static constexpr size_t kSensorsPresentBytes = 32;
    static constexpr size_t kSensorConfigBytes = 12;
    static constexpr uint8_t kRegProductId = 0x1C;
    static constexpr uint8_t kRegKernelVersion = 0x20;
    static constexpr uint8_t kRegBootStatus = 0x25;
    static constexpr uint8_t kRegErrorValue = 0x2E;
    static constexpr uint8_t kBootHostInterfaceReady = 0x10;
    static constexpr uint8_t kBootFirmwareVerifyDone = 0x20;
    static constexpr uint8_t kBootFirmwareVerifyError = 0x40;
    static constexpr uint16_t kCmdUploadToProgramRam = 0x0002;
    static constexpr uint16_t kCmdBootProgramRam = 0x0003;
    static constexpr uint16_t kCmdConfigureSensor = 0x000D;
    static constexpr uint16_t kCmdChangeRange = 0x000E;
    static constexpr uint8_t kSensorAccelerometer = 0x04;
    static constexpr uint8_t kSensorGyroscope = 0x0D;
    static constexpr uint8_t kSysIdTimestampSmallDelta = 251;
    static constexpr uint8_t kSysIdMetaEvent = 254;
    static constexpr uint8_t kSysIdMetaEventWakeup = 248;
    static constexpr uint8_t kMetaEventInitialised = 16;
    static constexpr uint8_t kInterruptFifoWakeup = 0x02;
    static constexpr int kCountsPerRange = 32768;

    void reset() {
        resets++;
        booted = false;
        uploaded_ = 0;
        magic_ = 0;
        command_ = 0;
        expected_ = 0;
        received_ = 0;
        payload_bytes_ = 0;
        sample_rate_hz = 0;
        gyro_rate_hz = 0;
        announced_ = false;
        status_len_ = 0;
        status_pos_ = 0;
        wakeup_ = Stream{};
        non_wakeup_ = Stream{};
    }

    bool accept_command(const uint8_t* data, size_t len) {
        size_t pos = 0;
        while (pos < len) {
            if (expected_ == 0 && received_ == 0 && command_ == 0) {
                if (len - pos < 4) return false;
                command_ = get_u16(data + pos);
                const uint16_t declared = get_u16(data + pos + 2);
                expected_ = command_ == kCmdUploadToProgramRam ? declared * 4u : declared;
                pos += 4;
                payload_bytes_ = 0;
                if (expected_ == 0) finish_command();
                continue;
            }
            const size_t take =
                len - pos < expected_ - received_ ? len - pos : expected_ - received_;
            for (size_t i = 0; i < take; i++) {
                if (command_ == kCmdUploadToProgramRam) {
                    if (received_ + i == 0) magic_ = data[pos + i];
                    if (received_ + i == 1)
                        magic_ = static_cast<uint16_t>(magic_ | (data[pos + i] << 8));
                } else if (payload_bytes_ < sizeof(payload_)) {
                    payload_[payload_bytes_++] = data[pos + i];
                }
            }
            received_ += take;
            pos += take;
            if (received_ >= expected_) finish_command();
        }
        return true;
    }

    void finish_command() {
        switch (command_) {
            case kCmdUploadToProgramRam: uploaded_ = received_; break;
            case kCmdBootProgramRam:
                booted = accepts_firmware && magic_ == kFirmwareMagic && uploaded_ > 0;
                verify_error = !booted;
                if (booted && announces_itself) report_meta_event(kMetaEventInitialised, 0, 0);
                break;
            case kCmdChangeRange:
                if (!announced_ || payload_bytes_ < 3) break;
                if (payload_[0] == kSensorAccelerometer) accel_range_g = get_u16(payload_ + 1);
                if (payload_[0] == kSensorGyroscope) gyro_range_dps = get_u16(payload_ + 1);
                break;
            case kCmdConfigureSensor: {
                if (!announced_ || !accepts_configuration || payload_bytes_ < 8) break;
                const uint32_t bits = get_u32(payload_ + 1);
                float rate = 0;
                std::memcpy(&rate, &bits, sizeof(rate));
                if (payload_[0] == kSensorGyroscope) {
                    gyro_sensor_id = payload_[0];
                    gyro_rate_hz = rate;
                    break;
                }
                accel_sensor_id = payload_[0];
                sample_rate_hz = rate;
                latency_ms =
                    static_cast<uint32_t>(payload_[5] | (payload_[6] << 8) | (payload_[7] << 16));
                break;
            }
            default:
                if (command_ & kParamReadMask) answer_parameter(command_ & ~kParamReadMask);
                break;
        }
        command_ = 0;
        expected_ = 0;
        received_ = 0;
    }

    uint8_t register_value(uint8_t reg) const {
        switch (reg) {
            case kRegProductId: return product_id;
            case kRegKernelVersion: return booted ? (kKernelVersion & 0xFF) : 0;
            case kRegKernelVersion + 1: return booted ? (kKernelVersion >> 8) : 0;
            case kRegBootStatus: return boot_status();
            case kRegInterruptStatus: return interrupt_status();
            case kRegErrorValue: return error_value;
            default: return 0;
        }
    }

    uint8_t boot_status() const {
        uint8_t status = host_interface_ready ? kBootHostInterfaceReady : 0;
        if (booted) status |= kBootFirmwareVerifyDone;
        if (verify_error && !booted) status |= kBootFirmwareVerifyError;
        return status;
    }

    uint8_t interrupt_status() const {
        uint8_t status = 0;
        if (status_pos_ < status_len_) status |= kInterruptStatusChannel;
        if (running() || meta_pending_ || holding(non_wakeup_)) status |= kInterruptFifoNonWakeup;
        if (meta_pending_wakeup_ || holding(wakeup_)) status |= kInterruptFifoWakeup;
        return status;
    }

    static bool holding(const Stream& stream) { return stream.pos < stream.len; }

    void answer_parameter(uint16_t param) {
        uint8_t payload[kSensorsPresentBytes] = {};
        size_t n = 0;
        if (param == kParamSensorsPresent) {
            n = kSensorsPresentBytes;
            if (accel_present)
                payload[kSensorAccelerometer / 8] |=
                    static_cast<uint8_t>(1 << (kSensorAccelerometer % 8));
            if (gyro_present)
                payload[kSensorGyroscope / 8] |= static_cast<uint8_t>(1 << (kSensorGyroscope % 8));
        } else if (param == kParamSensorConfig + kSensorAccelerometer) {
            n = kSensorConfigBytes;
            uint32_t bits = 0;
            std::memcpy(&bits, &sample_rate_hz, sizeof(bits));
            put_u32(payload, bits);
            put_u16(payload + 10, accel_range_g);
        } else {
            return;
        }

        status_len_ = 0;
        status_pos_ = 0;
        status_[status_len_++] = static_cast<uint8_t>(param & 0xFF);
        status_[status_len_++] = static_cast<uint8_t>(param >> 8);
        status_[status_len_++] = static_cast<uint8_t>(n & 0xFF);
        status_[status_len_++] = static_cast<uint8_t>(n >> 8);
        for (size_t i = 0; i < n; i++) status_[status_len_++] = payload[i];
    }

    bool read_status(uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++)
            data[i] = status_pos_ < status_len_ ? status_[status_pos_++] : 0;
        return true;
    }

    bool read_fifo(Stream& stream, uint8_t* data, size_t len) {
        if (stream.pos >= stream.len) fill_stream(stream);
        for (size_t i = 0; i < len; i++)
            data[i] = stream.pos < stream.len ? stream.bytes[stream.pos++] : 0;
        return true;
    }

    void fill_stream(Stream& stream) {
        const bool wakeup = &stream == &wakeup_;
        stream.len = 0;
        stream.pos = 0;
        uint8_t events[32];
        int n = 0;
        bool& pending = wakeup ? meta_pending_wakeup_ : meta_pending_;
        if (pending) {
            events[n++] = wakeup ? kSysIdMetaEventWakeup : kSysIdMetaEvent;
            events[n++] = meta_event_;
            events[n++] = meta_first_;
            events[n++] = meta_second_;
            pending = false;
            if (meta_event_ == kMetaEventInitialised && !wakeup) announced_ = true;
        }
        if (running() && !wakeup) {
            events[n++] = kSysIdTimestampSmallDelta;
            events[n++] = 1;
            events[n++] = kSensorAccelerometer;
            n += put_counts(events + n, x_mg, accel_range_g == 0 ? 1 : accel_range_g, 1000);
            n += put_counts(events + n, y_mg, accel_range_g == 0 ? 1 : accel_range_g, 1000);
            n += put_counts(events + n, z_mg, accel_range_g == 0 ? 1 : accel_range_g, 1000);
        }
        if (gyro_streaming() && !wakeup) {
            events[n++] = kSensorGyroscope;
            n += put_counts(events + n, x_cdps, gyro_range_dps == 0 ? 1 : gyro_range_dps, 100);
            n += put_counts(events + n, y_cdps, gyro_range_dps == 0 ? 1 : gyro_range_dps, 100);
            n += put_counts(events + n, z_cdps, gyro_range_dps == 0 ? 1 : gyro_range_dps, 100);
        }
        stream.bytes[stream.len++] = static_cast<uint8_t>(n & 0xFF);
        stream.bytes[stream.len++] = static_cast<uint8_t>(n >> 8);
        for (int i = 0; i < n; i++) stream.bytes[stream.len++] = events[i];
    }

    static int put_counts(uint8_t* out, int16_t value, int range, int per_unit) {
        const int32_t counts = static_cast<int32_t>(value) * kCountsPerRange / (range * per_unit);
        put_u16(out, static_cast<uint16_t>(counts));
        return 2;
    }

    uint8_t pointer{0};
    uint16_t command_{0};
    uint32_t expected_{0};
    uint32_t received_{0};
    uint32_t uploaded_{0};
    uint16_t magic_{0};
    uint8_t payload_[8]{};
    size_t payload_bytes_{0};
    uint8_t meta_event_{0};
    uint8_t meta_first_{0};
    uint8_t meta_second_{0};
    bool meta_pending_{false};
    bool meta_pending_wakeup_{false};
    bool announced_{false};
    Stream wakeup_{};
    Stream non_wakeup_{};
    uint8_t status_[4 + kSensorsPresentBytes]{};
    size_t status_len_{0};
    size_t status_pos_{0};
};

}  // namespace skyblip::models

#endif
