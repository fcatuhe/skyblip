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
        size_t held{0};
        size_t position{0};
    };

    bool write(uint8_t address, const uint8_t* data, size_t bytes) override {
        if (address != bus_address || !answers) return false;
        if (bytes == 0) return true;
        pointer = data[0];
        if (pointer == kRegisterCommand) return accept_command(data + 1, bytes - 1);
        for (size_t i = 1; i < bytes; i++) {
            const uint8_t register_address = static_cast<uint8_t>(pointer + i - 1);
            if (register_address == kRegisterResetRequest && data[i] != 0) reset();
        }
        return true;
    }

    bool read(uint8_t address, uint8_t* out, size_t bytes) override {
        if (address != bus_address || !answers) return false;
        if (pointer == kRegisterFifoWakeup) return read_fifo(wakeup_, out, bytes);
        if (pointer == kRegisterFifoNonWakeup) return read_fifo(non_wakeup_, out, bytes);
        if (pointer == kRegisterStatusChannel) return read_status(out, bytes);
        for (size_t i = 0; i < bytes; i++)
            out[i] = register_value(static_cast<uint8_t>(pointer + i));
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
    bool gyroscope_streaming() const { return booted && gyroscope_rate_hz > 0; }
    uint32_t uploaded_bytes() const { return uploaded_bytes_; }

    uint8_t bus_address{kAddress};
    bool answers{true};
    bool accepts_firmware{true};
    uint8_t product_id{0x89};
    bool announces_itself{true};
    bool accelerometer_present{true};
    bool gyroscope_present{true};
    bool accepts_configuration{true};
    uint8_t error_value{0};
    bool host_interface_ready{true};
    bool booted{false};
    bool verify_error{false};
    uint8_t accelerometer_sensor_id{0};
    uint16_t accelerometer_range_g{0};
    uint8_t gyroscope_sensor_id{0};
    uint16_t gyroscope_range_dps{0};
    float gyroscope_rate_hz{0};
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
    static constexpr uint8_t kRegisterCommand = 0x00;
    static constexpr uint8_t kRegisterFifoWakeup = 0x01;
    static constexpr uint8_t kRegisterFifoNonWakeup = 0x02;
    static constexpr uint8_t kRegisterStatusChannel = 0x03;
    static constexpr uint8_t kRegisterResetRequest = 0x14;
    static constexpr uint8_t kRegisterInterruptStatus = 0x2D;
    static constexpr uint8_t kInterruptFifoNonWakeup = 0x18;
    static constexpr uint8_t kInterruptStatusChannel = 0x20;
    static constexpr uint16_t kParameterReadMask = 0x1000;
    static constexpr uint16_t kParameterSensorsPresent = 0x011F;
    static constexpr uint16_t kParameterSensorConfig = 0x0500;
    static constexpr size_t kSensorsPresentBytes = 32;
    static constexpr size_t kSensorConfigBytes = 12;
    static constexpr uint8_t kRegisterProductId = 0x1C;
    static constexpr uint8_t kRegisterKernelVersion = 0x20;
    static constexpr uint8_t kRegisterBootStatus = 0x25;
    static constexpr uint8_t kRegisterErrorValue = 0x2E;
    static constexpr uint8_t kBootHostInterfaceReady = 0x10;
    static constexpr uint8_t kBootFirmwareVerifyDone = 0x20;
    static constexpr uint8_t kBootFirmwareVerifyError = 0x40;
    static constexpr uint16_t kCommandUploadToProgramRam = 0x0002;
    static constexpr uint16_t kCommandBootProgramRam = 0x0003;
    static constexpr uint16_t kCommandConfigureSensor = 0x000D;
    static constexpr uint16_t kCommandChangeRange = 0x000E;
    static constexpr uint8_t kSensorAccelerometer = 0x04;
    static constexpr uint8_t kSensorGyroscope = 0x0D;
    static constexpr uint8_t kSystemEventIdTimestampSmallDelta = 251;
    static constexpr uint8_t kSystemEventIdMetaEvent = 254;
    static constexpr uint8_t kSystemEventIdMetaEventWakeup = 248;
    static constexpr uint8_t kMetaEventInitialised = 16;
    static constexpr uint8_t kInterruptFifoWakeup = 0x02;
    static constexpr int kCountsPerRange = 32768;

    void reset() {
        resets++;
        booted = false;
        uploaded_bytes_ = 0;
        magic_ = 0;
        command_ = 0;
        expected_bytes_ = 0;
        received_bytes_ = 0;
        payload_bytes_ = 0;
        sample_rate_hz = 0;
        gyroscope_rate_hz = 0;
        announced_ = false;
        status_bytes_ = 0;
        status_position_ = 0;
        wakeup_ = Stream{};
        non_wakeup_ = Stream{};
    }

    bool accept_command(const uint8_t* data, size_t bytes) {
        size_t position = 0;
        while (position < bytes) {
            if (expected_bytes_ == 0 && received_bytes_ == 0 && command_ == 0) {
                if (bytes - position < 4) return false;
                command_ = get_uint16(data + position);
                const uint16_t declared = get_uint16(data + position + 2);
                expected_bytes_ = command_ == kCommandUploadToProgramRam ? declared * 4u : declared;
                position += 4;
                payload_bytes_ = 0;
                if (expected_bytes_ == 0) finish_command();
                continue;
            }
            const size_t take = bytes - position < expected_bytes_ - received_bytes_
                                    ? bytes - position
                                    : expected_bytes_ - received_bytes_;
            for (size_t i = 0; i < take; i++) {
                if (command_ == kCommandUploadToProgramRam) {
                    if (received_bytes_ + i == 0) magic_ = data[position + i];
                    if (received_bytes_ + i == 1)
                        magic_ = static_cast<uint16_t>(magic_ | (data[position + i] << 8));
                } else if (payload_bytes_ < sizeof(payload_)) {
                    payload_[payload_bytes_++] = data[position + i];
                }
            }
            received_bytes_ += take;
            position += take;
            if (received_bytes_ >= expected_bytes_) finish_command();
        }
        return true;
    }

    void finish_command() {
        switch (command_) {
            case kCommandUploadToProgramRam: uploaded_bytes_ = received_bytes_; break;
            case kCommandBootProgramRam:
                booted = accepts_firmware && magic_ == kFirmwareMagic && uploaded_bytes_ > 0;
                verify_error = !booted;
                if (booted && announces_itself) report_meta_event(kMetaEventInitialised, 0, 0);
                break;
            case kCommandChangeRange:
                if (!announced_ || payload_bytes_ < 3) break;
                if (payload_[0] == kSensorAccelerometer)
                    accelerometer_range_g = get_uint16(payload_ + 1);
                if (payload_[0] == kSensorGyroscope) gyroscope_range_dps = get_uint16(payload_ + 1);
                break;
            case kCommandConfigureSensor: {
                if (!announced_ || !accepts_configuration || payload_bytes_ < 8) break;
                const uint32_t bits = get_uint32(payload_ + 1);
                float rate = 0;
                std::memcpy(&rate, &bits, sizeof(rate));
                if (payload_[0] == kSensorGyroscope) {
                    gyroscope_sensor_id = payload_[0];
                    gyroscope_rate_hz = rate;
                    break;
                }
                accelerometer_sensor_id = payload_[0];
                sample_rate_hz = rate;
                latency_ms =
                    static_cast<uint32_t>(payload_[5] | (payload_[6] << 8) | (payload_[7] << 16));
                break;
            }
            default:
                if (command_ & kParameterReadMask) answer_parameter(command_ & ~kParameterReadMask);
                break;
        }
        command_ = 0;
        expected_bytes_ = 0;
        received_bytes_ = 0;
    }

    uint8_t register_value(uint8_t register_address) const {
        switch (register_address) {
            case kRegisterProductId: return product_id;
            case kRegisterKernelVersion: return booted ? (kKernelVersion & 0xFF) : 0;
            case kRegisterKernelVersion + 1: return booted ? (kKernelVersion >> 8) : 0;
            case kRegisterBootStatus: return boot_status();
            case kRegisterInterruptStatus: return interrupt_status();
            case kRegisterErrorValue: return error_value;
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
        if (status_position_ < status_bytes_) status |= kInterruptStatusChannel;
        if (running() || meta_pending_ || holding(non_wakeup_)) status |= kInterruptFifoNonWakeup;
        if (meta_pending_wakeup_ || holding(wakeup_)) status |= kInterruptFifoWakeup;
        return status;
    }

    static bool holding(const Stream& stream) { return stream.position < stream.held; }

    void answer_parameter(uint16_t parameter) {
        uint8_t payload[kSensorsPresentBytes] = {};
        size_t answer_bytes = 0;
        if (parameter == kParameterSensorsPresent) {
            answer_bytes = kSensorsPresentBytes;
            if (accelerometer_present)
                payload[kSensorAccelerometer / 8] |=
                    static_cast<uint8_t>(1 << (kSensorAccelerometer % 8));
            if (gyroscope_present)
                payload[kSensorGyroscope / 8] |= static_cast<uint8_t>(1 << (kSensorGyroscope % 8));
        } else if (parameter == kParameterSensorConfig + kSensorAccelerometer) {
            answer_bytes = kSensorConfigBytes;
            uint32_t bits = 0;
            std::memcpy(&bits, &sample_rate_hz, sizeof(bits));
            put_uint32(payload, bits);
            put_uint16(payload + 10, accelerometer_range_g);
        } else {
            return;
        }

        status_bytes_ = 0;
        status_position_ = 0;
        status_[status_bytes_++] = static_cast<uint8_t>(parameter & 0xFF);
        status_[status_bytes_++] = static_cast<uint8_t>(parameter >> 8);
        status_[status_bytes_++] = static_cast<uint8_t>(answer_bytes & 0xFF);
        status_[status_bytes_++] = static_cast<uint8_t>(answer_bytes >> 8);
        for (size_t i = 0; i < answer_bytes; i++) status_[status_bytes_++] = payload[i];
    }

    bool read_status(uint8_t* out, size_t bytes) {
        for (size_t i = 0; i < bytes; i++)
            out[i] = status_position_ < status_bytes_ ? status_[status_position_++] : 0;
        return true;
    }

    bool read_fifo(Stream& stream, uint8_t* out, size_t bytes) {
        if (stream.position >= stream.held) fill_stream(stream);
        for (size_t i = 0; i < bytes; i++)
            out[i] = stream.position < stream.held ? stream.bytes[stream.position++] : 0;
        return true;
    }

    void fill_stream(Stream& stream) {
        const bool wakeup = &stream == &wakeup_;
        stream.held = 0;
        stream.position = 0;
        uint8_t events[32];
        int written = 0;
        bool& pending = wakeup ? meta_pending_wakeup_ : meta_pending_;
        if (pending) {
            events[written++] = wakeup ? kSystemEventIdMetaEventWakeup : kSystemEventIdMetaEvent;
            events[written++] = meta_event_;
            events[written++] = meta_first_;
            events[written++] = meta_second_;
            pending = false;
            if (meta_event_ == kMetaEventInitialised && !wakeup) announced_ = true;
        }
        if (running() && !wakeup) {
            events[written++] = kSystemEventIdTimestampSmallDelta;
            events[written++] = 1;
            events[written++] = kSensorAccelerometer;
            written += put_counts(events + written, x_mg,
                                  accelerometer_range_g == 0 ? 1 : accelerometer_range_g, 1000);
            written += put_counts(events + written, y_mg,
                                  accelerometer_range_g == 0 ? 1 : accelerometer_range_g, 1000);
            written += put_counts(events + written, z_mg,
                                  accelerometer_range_g == 0 ? 1 : accelerometer_range_g, 1000);
        }
        if (gyroscope_streaming() && !wakeup) {
            events[written++] = kSensorGyroscope;
            written += put_counts(events + written, x_cdps,
                                  gyroscope_range_dps == 0 ? 1 : gyroscope_range_dps, 100);
            written += put_counts(events + written, y_cdps,
                                  gyroscope_range_dps == 0 ? 1 : gyroscope_range_dps, 100);
            written += put_counts(events + written, z_cdps,
                                  gyroscope_range_dps == 0 ? 1 : gyroscope_range_dps, 100);
        }
        stream.bytes[stream.held++] = static_cast<uint8_t>(written & 0xFF);
        stream.bytes[stream.held++] = static_cast<uint8_t>(written >> 8);
        for (int i = 0; i < written; i++) stream.bytes[stream.held++] = events[i];
    }

    static int put_counts(uint8_t* out, int16_t value, int range, int per_unit) {
        const int32_t counts = static_cast<int32_t>(value) * kCountsPerRange / (range * per_unit);
        put_uint16(out, static_cast<uint16_t>(counts));
        return 2;
    }

    uint8_t pointer{0};
    uint16_t command_{0};
    uint32_t expected_bytes_{0};
    uint32_t received_bytes_{0};
    uint32_t uploaded_bytes_{0};
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
    size_t status_bytes_{0};
    size_t status_position_{0};
};

}  // namespace skyblip::models

#endif
