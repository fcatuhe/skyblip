#ifndef SKYBLIP_HARDWARE_PARTS_BHI260_H
#define SKYBLIP_HARDWARE_PARTS_BHI260_H

#include <cstdint>

#include "core/util/result.h"
#include "core/util/span.h"
#include "hardware/io/io.h"

namespace skyblip::parts {

struct Acceleration {
    int16_t x_mg{0};
    int16_t y_mg{0};
    int16_t z_mg{0};
    uint32_t at_ms{0};
};

class Bhi260 {
   public:
    class Image {
       public:
        virtual ~Image() = default;
        virtual uint32_t size() const = 0;
        virtual bool read(uint32_t offset, uint8_t* out, uint16_t len) = 0;
    };

    static constexpr uint8_t kAddress = 0x28;
    static constexpr uint8_t kAddressAlternate = 0x29;
    static constexpr uint8_t kProductId = 0x89;

    static constexpr int32_t kRangeG = 4;
    static constexpr uint32_t kSampleRateBits = 0x41480000;
    static constexpr uint32_t kSamplePeriodMs = 200;
    static constexpr uint16_t kUploadChunkBytes = 240;
    static constexpr uint16_t kFifoReadBytes = 64;

    static constexpr uint32_t kResetSettleMs = 100;
    static constexpr uint32_t kStatusPollMs = 50;
    static constexpr uint32_t kHostInterfaceTimeoutMs = 2000;
    static constexpr uint32_t kBootTimeoutMs = 5000;
    static constexpr uint32_t kInitialisedTimeoutMs = 2000;
    static constexpr uint32_t kParameterTimeoutMs = 500;

    enum class Stage : uint8_t {
        Absent,
        Idle,
        Resetting,
        HostInterface,
        Uploading,
        Booting,
        Initialising,
        Configuring,
        Running,
        Failed,
    };

    explicit Bhi260(io::I2c& bus) : bus_(bus) {}

    Status probe();
    void load(Image& image, uint32_t now_ms);
    void service(uint32_t now_ms);
    bool poll();

    const Acceleration& acceleration() const { return sample_; }
    Stage stage() const { return stage_; }
    Status fault() const { return fault_; }
    const char* stage_text() const;
    const char* fault_text() const;
    bool running() const { return stage_ == Stage::Running; }
    uint8_t address() const { return address_; }
    uint16_t kernel_version() const { return kernel_version_; }
    uint32_t uploaded_bytes() const { return uploaded_; }
    uint32_t unparsed_events() const { return unparsed_; }
    uint32_t fifo_bytes() const { return fifo_bytes_; }
    uint8_t hub_error() const { return error_; }
    uint8_t meta_event() const { return meta_event_; }
    uint8_t interrupt_status() const { return interrupt_; }
    uint8_t sensor_error() const { return sensor_error_; }
    uint8_t errored_sensor() const { return errored_sensor_; }

   private:
    enum class Setup : uint8_t {
        Kernel,
        AwaitSensorsPresent,
        AwaitConfiguration,
    };

    struct Fifo {
        uint8_t reg{0};
        uint16_t remaining{0};
        uint16_t carried{0};
        bool resync{false};
        uint8_t bytes[kFifoReadBytes]{};
    };

    static constexpr uint8_t kRegCommand = 0x00;
    static constexpr uint8_t kRegFifoWakeup = 0x01;
    static constexpr uint8_t kRegFifoNonWakeup = 0x02;
    static constexpr uint8_t kRegStatusChannel = 0x03;
    static constexpr uint8_t kRegChipControl = 0x05;
    static constexpr uint8_t kRegHostInterfaceControl = 0x06;
    static constexpr uint8_t kRegResetRequest = 0x14;
    static constexpr uint8_t kRegProductId = 0x1C;
    static constexpr uint8_t kRegKernelVersion = 0x20;
    static constexpr uint8_t kRegBootStatus = 0x25;
    static constexpr uint8_t kRegInterruptStatus = 0x2D;
    static constexpr uint8_t kRegErrorValue = 0x2E;

    static constexpr uint16_t kCmdUploadToProgramRam = 0x0002;
    static constexpr uint16_t kCmdBootProgramRam = 0x0003;
    static constexpr uint16_t kCmdConfigureSensor = 0x000D;
    static constexpr uint16_t kCmdChangeRange = 0x000E;

    static constexpr uint16_t kParamReadMask = 0x1000;
    static constexpr uint16_t kParamSensorsPresent = 0x011F;
    static constexpr uint16_t kParamSensorConfig = 0x0500;
    static constexpr uint16_t kSensorsPresentBytes = 32;
    static constexpr uint16_t kSensorConfigBytes = 12;
    static constexpr uint8_t kInterruptStatusChannel = 0x20;

    static constexpr uint8_t kBootHostInterfaceReady = 0x10;
    static constexpr uint8_t kBootFirmwareVerifyDone = 0x20;
    static constexpr uint8_t kBootFirmwareVerifyError = 0x40;

    static constexpr uint16_t kFirmwareMagic = 0x662B;
    static constexpr uint8_t kErrorHostChannelEmpty = 0x77;
    static constexpr uint8_t kSensorAccelerometer = 0x04;
    static constexpr uint8_t kAccelEventBytes = 7;
    static constexpr int32_t kCountsPerRange = 32768;
    static constexpr uint16_t kCommandHeaderBytes = 4;

    bool write_registers(uint8_t reg, const uint8_t* data, uint16_t len);
    bool read_registers(uint8_t reg, uint8_t* out, uint16_t len);
    bool command(uint16_t cmd, const uint8_t* payload, uint16_t len);
    bool boot_status(uint8_t& out);
    void fail(Status why);

    void step_reset(uint32_t now_ms);
    void step_host_interface(uint32_t now_ms);
    void step_upload(uint32_t now_ms);
    void step_boot(uint32_t now_ms);
    void step_initialise(uint32_t now_ms);
    void step_configure(uint32_t now_ms);
    void read_kernel_version(uint32_t now_ms);
    void check_accelerometer_present(uint32_t now_ms);
    void send_configuration(uint32_t now_ms);
    void confirm_configuration(uint32_t now_ms);
    void start_running(uint32_t now_ms);
    bool request_parameter(uint16_t param);
    bool parameter_ready();
    int read_status_channel(uint16_t& code, uint8_t* out, uint16_t max);
    void step_running(uint32_t now_ms);

    bool configure_sensor(uint8_t sensor, int32_t range);
    void read_hub_error();
    void drain_fifos(uint32_t now_ms);
    void drain_fifo(Fifo& fifo, uint32_t now_ms);
    uint16_t parse_fifo(Fifo& fifo, uint16_t len, uint32_t now_ms);
    void note_meta_event(const uint8_t* event);
    static uint8_t event_bytes(uint8_t id);
    static int16_t to_milli_g(const uint8_t* le16);

    io::I2c& bus_;
    Image* image_{nullptr};
    Stage stage_{Stage::Absent};
    Status fault_{Status::Ok};
    uint8_t address_{kAddress};
    Stage failed_stage_{Stage::Absent};
    uint32_t since_ms_{0};
    uint32_t polled_ms_{0};
    uint32_t uploaded_{0};
    uint32_t unparsed_{0};
    uint32_t fifo_bytes_{0};
    Setup setup_{Setup::Kernel};
    uint8_t error_{0};
    uint8_t interrupt_{0};
    uint8_t meta_event_{0};
    uint8_t sensor_error_{0};
    uint8_t errored_sensor_{0};
    uint16_t kernel_version_{0};
    Acceleration sample_{};
    bool fresh_{false};
    uint8_t frame_[1 + kCommandHeaderBytes + kUploadChunkBytes]{};
    Fifo wakeup_{kRegFifoWakeup};
    Fifo non_wakeup_{kRegFifoNonWakeup};
};

class LinkedImage : public Bhi260::Image {
   public:
    explicit LinkedImage(ConstByteSpan bytes) : bytes_(bytes) {}

    uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
    bool read(uint32_t offset, uint8_t* out, uint16_t len) override;

   private:
    ConstByteSpan bytes_;
};

}  // namespace skyblip::parts

#endif
