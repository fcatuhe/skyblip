#include "hardware/parts/bhi260/bhi260.h"

namespace skyblip::parts {

namespace {

constexpr uint8_t kSysIdPadding = 0x00;
constexpr uint8_t kSysIdFirst = 245;
constexpr uint8_t kSysEventBytes[] = {2, 3, 6, 4, 0, 18, 2, 3, 6, 4, 1};
constexpr uint8_t kSysIdMetaEventWakeup = 248;
constexpr uint8_t kSysIdMetaEvent = 254;
constexpr uint8_t kMetaEventSensorError = 11;
constexpr uint8_t kMetaEventInitialised = 16;

uint16_t le16(const uint8_t* bytes) { return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8)); }

uint32_t le32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

bool bit_set(const uint8_t* bitmap, uint8_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

}  // namespace

Status Bhi260::probe() {
    const uint8_t candidates[] = {kAddress, kAddressAlternate};
    Status answer = Status::Down;
    for (uint8_t candidate : candidates) {
        if (!bus_.write(candidate, nullptr, 0)) continue;
        address_ = candidate;
        uint8_t id = 0;
        if (!read_registers(kRegProductId, &id, 1)) continue;
        if (id == kProductId) {
            stage_ = Stage::Idle;
            return Status::Ok;
        }
        answer = Status::Unsupported;
    }
    address_ = kAddress;
    return answer;
}

void Bhi260::load(Image& image, uint32_t now_ms) {
    image_ = &image;
    uploaded_ = 0;
    wakeup_ = Fifo{kRegFifoWakeup};
    non_wakeup_ = Fifo{kRegFifoNonWakeup};
    fresh_ = false;
    fault_ = Status::Ok;
    meta_event_ = 0;
    sensor_error_ = 0;
    errored_sensor_ = 0;
    interrupt_ = 0;

    uint8_t magic[2] = {0};
    if (image.size() < kCommandHeaderBytes || !image.read(0, magic, sizeof(magic)) ||
        le16(magic) != kFirmwareMagic) {
        fail(Status::Invalid);
        return;
    }

    const uint8_t request = 1;
    if (!write_registers(kRegResetRequest, &request, 1)) {
        fail(Status::Down);
        return;
    }
    stage_ = Stage::Resetting;
    since_ms_ = now_ms;
}

void Bhi260::service(uint32_t now_ms) {
    switch (stage_) {
        case Stage::Resetting: step_reset(now_ms); return;
        case Stage::HostInterface: step_host_interface(now_ms); return;
        case Stage::Uploading: step_upload(now_ms); return;
        case Stage::Booting: step_boot(now_ms); return;
        case Stage::Initialising: step_initialise(now_ms); return;
        case Stage::Configuring: step_configure(now_ms); return;
        case Stage::Running: step_running(now_ms); return;
        case Stage::Absent:
        case Stage::Idle:
        case Stage::Failed: return;
    }
}

bool Bhi260::poll() {
    const bool fresh = fresh_;
    fresh_ = false;
    return fresh;
}

const char* Bhi260::stage_text() const {
    switch (stage_ == Stage::Failed ? failed_stage_ : stage_) {
        case Stage::Absent: return "NONE";
        case Stage::Idle: return "IDLE";
        case Stage::Resetting: return "RST";
        case Stage::HostInterface: return "HIF";
        case Stage::Uploading: return "LOAD";
        case Stage::Booting: return "BOOT";
        case Stage::Initialising: return "INIT";
        case Stage::Configuring: return "CONF";
        case Stage::Running: return "RUN";
        case Stage::Failed: return "FAIL";
    }
    return "?";
}

const char* Bhi260::fault_text() const {
    if (stage_ != Stage::Failed) return "";
    switch (fault_) {
        case Status::Invalid: return "IMAGE";
        case Status::Crc: return "VERIFY";
        case Status::Timeout: return "TIMEOUT";
        case Status::NotFound: return "NOSENS";
        case Status::Unsupported: return "NOCFG";

        default: return "DOWN";
    }
}

void Bhi260::step_reset(uint32_t now_ms) {
    if (now_ms - since_ms_ < kResetSettleMs) return;
    stage_ = Stage::HostInterface;
    since_ms_ = now_ms;
    polled_ms_ = now_ms - kStatusPollMs;
}

void Bhi260::step_host_interface(uint32_t now_ms) {
    if (now_ms - polled_ms_ < kStatusPollMs) return;
    polled_ms_ = now_ms;

    uint8_t status = 0;
    if (!boot_status(status)) return;
    if (status & kBootHostInterfaceReady) {
        stage_ = Stage::Uploading;
        since_ms_ = now_ms;
        return;
    }
    if (now_ms - since_ms_ >= kHostInterfaceTimeoutMs) fail(Status::Timeout);
}

void Bhi260::step_upload(uint32_t now_ms) {
    const bool first = uploaded_ == 0;
    const uint16_t header = first ? kCommandHeaderBytes : 0;
    const uint32_t left = image_->size() - uploaded_;
    uint16_t payload = static_cast<uint16_t>(kUploadChunkBytes - header);
    if (payload > left) payload = static_cast<uint16_t>(left);

    uint16_t n = 0;
    frame_[n++] = kRegCommand;
    if (first) {
        const uint32_t words = (image_->size() + 3) / 4;
        frame_[n++] = static_cast<uint8_t>(kCmdUploadToProgramRam & 0xFF);
        frame_[n++] = static_cast<uint8_t>(kCmdUploadToProgramRam >> 8);
        frame_[n++] = static_cast<uint8_t>(words & 0xFF);
        frame_[n++] = static_cast<uint8_t>((words >> 8) & 0xFF);
    }
    if (!image_->read(uploaded_, frame_ + n, payload)) {
        fail(Status::Down);
        return;
    }
    n = static_cast<uint16_t>(n + payload);
    while ((n - 1) % 4 != 0) frame_[n++] = 0;

    if (!bus_.write(address_, frame_, n)) {
        fail(Status::Down);
        return;
    }
    uploaded_ += payload;
    if (uploaded_ < image_->size()) return;

    if (!command(kCmdBootProgramRam, nullptr, 0)) {
        fail(Status::Down);
        return;
    }
    stage_ = Stage::Booting;
    since_ms_ = now_ms;
    polled_ms_ = now_ms;
}

void Bhi260::step_boot(uint32_t now_ms) {
    if (now_ms - polled_ms_ < kStatusPollMs) return;
    polled_ms_ = now_ms;

    uint8_t status = 0;
    if (!boot_status(status)) return;
    if (status & kBootFirmwareVerifyError) {
        fail(Status::Crc);
        return;
    }
    if ((status & kBootHostInterfaceReady) && (status & kBootFirmwareVerifyDone)) {
        stage_ = Stage::Initialising;
        since_ms_ = now_ms;
        polled_ms_ = now_ms;
        return;
    }
    if (now_ms - since_ms_ >= kBootTimeoutMs) fail(Status::Timeout);
}

void Bhi260::step_initialise(uint32_t now_ms) {
    if (now_ms - polled_ms_ < kStatusPollMs) return;
    polled_ms_ = now_ms;

    drain_fifos(now_ms);
    if (stage_ != Stage::Initialising) return;
    if (meta_event_ == kMetaEventInitialised || now_ms - since_ms_ >= kInitialisedTimeoutMs) {
        stage_ = Stage::Configuring;
        setup_ = Setup::Kernel;
    }
}

void Bhi260::step_configure(uint32_t now_ms) {
    switch (setup_) {
        case Setup::Kernel: read_kernel_version(now_ms); return;
        case Setup::AwaitSensorsPresent: check_accelerometer_present(now_ms); return;
        case Setup::AwaitConfiguration: confirm_configuration(now_ms); return;
    }
}

void Bhi260::read_kernel_version(uint32_t now_ms) {
    uint8_t version[2] = {0, 0};
    if (!read_registers(kRegKernelVersion, version, sizeof(version))) {
        fail(Status::Down);
        return;
    }
    kernel_version_ = le16(version);
    if (kernel_version_ == 0) {
        fail(Status::Down);
        return;
    }
    if (!request_parameter(kParamSensorsPresent)) {
        fail(Status::Down);
        return;
    }
    setup_ = Setup::AwaitSensorsPresent;
    since_ms_ = now_ms;
}

void Bhi260::check_accelerometer_present(uint32_t now_ms) {
    if (!parameter_ready()) {
        if (now_ms - since_ms_ >= kParameterTimeoutMs) send_configuration(now_ms);
        return;
    }

    uint16_t code = 0;
    uint8_t present[kSensorsPresentBytes] = {};
    const int n = read_status_channel(code, present, sizeof(present));
    if (n < 0) {
        fail(Status::Down);
        return;
    }
    if (code == kParamSensorsPresent && n == kSensorsPresentBytes) {
        if (!bit_set(present, kSensorAccelerometer)) {
            fail(Status::NotFound);
            return;
        }
    }
    send_configuration(now_ms);
}

void Bhi260::send_configuration(uint32_t now_ms) {
    if (!configure_sensor(kSensorAccelerometer, kRangeG)) {
        fail(Status::Down);
        return;
    }
    if (!request_parameter(kParamSensorConfig + kSensorAccelerometer)) {
        fail(Status::Down);
        return;
    }
    setup_ = Setup::AwaitConfiguration;
    since_ms_ = now_ms;
}

void Bhi260::confirm_configuration(uint32_t now_ms) {
    if (!parameter_ready()) {
        if (now_ms - since_ms_ >= kParameterTimeoutMs) start_running(now_ms);
        return;
    }

    uint16_t code = 0;
    uint8_t config[kSensorConfigBytes] = {};
    const int n = read_status_channel(code, config, sizeof(config));
    if (n < 0) {
        fail(Status::Down);
        return;
    }
    if (code == kParamSensorConfig + kSensorAccelerometer && n >= 4 && le32(config) == 0) {
        fail(Status::Unsupported);
        return;
    }
    start_running(now_ms);
}

void Bhi260::start_running(uint32_t now_ms) {
    stage_ = Stage::Running;
    polled_ms_ = now_ms;
}

bool Bhi260::request_parameter(uint16_t param) {
    return command(static_cast<uint16_t>(param | kParamReadMask), nullptr, 0);
}

bool Bhi260::parameter_ready() {
    uint8_t status = 0;
    if (!read_registers(kRegInterruptStatus, &status, 1)) return false;
    interrupt_ = status;
    return (status & kInterruptStatusChannel) != 0;
}

int Bhi260::read_status_channel(uint16_t& code, uint8_t* out, uint16_t max) {
    uint8_t header[4] = {};
    if (!read_registers(kRegStatusChannel, header, sizeof(header))) return -1;
    code = le16(header);
    const uint16_t len = le16(header + 2);
    if (len == 0) return 0;
    if (len > max) return -1;
    if (!read_registers(kRegStatusChannel, out, len)) return -1;
    return len;
}

bool Bhi260::configure_sensor(uint8_t sensor, int32_t range_units) {
    const uint8_t range[4] = {sensor, static_cast<uint8_t>(range_units & 0xFF),
                              static_cast<uint8_t>(range_units >> 8), 0};
    if (!command(kCmdChangeRange, range, sizeof(range))) return false;

    const uint8_t config[8] = {sensor,
                               static_cast<uint8_t>(kSampleRateBits & 0xFF),
                               static_cast<uint8_t>((kSampleRateBits >> 8) & 0xFF),
                               static_cast<uint8_t>((kSampleRateBits >> 16) & 0xFF),
                               static_cast<uint8_t>((kSampleRateBits >> 24) & 0xFF),
                               0,
                               0,
                               0};
    return command(kCmdConfigureSensor, config, sizeof(config));
}

void Bhi260::step_running(uint32_t now_ms) {
    if (now_ms - polled_ms_ < kSamplePeriodMs) return;
    polled_ms_ = now_ms;
    read_hub_error();
    drain_fifos(now_ms);
}

void Bhi260::read_hub_error() {
    uint8_t value = 0;
    if (!read_registers(kRegErrorValue, &value, 1)) return;
    error_ = value == kErrorHostChannelEmpty ? 0 : value;
    read_registers(kRegInterruptStatus, &interrupt_, 1);
}

void Bhi260::drain_fifos(uint32_t now_ms) {
    drain_fifo(non_wakeup_, now_ms);
    if (stage_ == Stage::Failed) return;
    drain_fifo(wakeup_, now_ms);
}

void Bhi260::drain_fifo(Fifo& fifo, uint32_t now_ms) {
    if (fifo.remaining == 0) {
        uint8_t available[2] = {0, 0};
        if (!read_registers(fifo.reg, available, sizeof(available))) {
            fail(Status::Down);
            return;
        }
        fifo.remaining = le16(available);
        fifo.resync = false;
        fifo.carried = 0;
        if (fifo.remaining == 0) return;
    }

    const uint16_t room = static_cast<uint16_t>(kFifoReadBytes - fifo.carried);
    const uint16_t want = fifo.remaining < room ? fifo.remaining : room;
    if (want == 0) {
        fifo.carried = 0;
        return;
    }
    if (!read_registers(fifo.reg, fifo.bytes + fifo.carried, want)) {
        fail(Status::Down);
        return;
    }
    fifo.remaining = static_cast<uint16_t>(fifo.remaining - want);
    fifo_bytes_ += want;
    if (fifo.resync) return;

    const uint16_t len = static_cast<uint16_t>(fifo.carried + want);
    const uint16_t used = parse_fifo(fifo, len, now_ms);
    fifo.carried = static_cast<uint16_t>(len - used);
    for (uint16_t i = 0; i < fifo.carried; i++) fifo.bytes[i] = fifo.bytes[used + i];
}

uint16_t Bhi260::parse_fifo(Fifo& fifo, uint16_t len, uint32_t now_ms) {
    const uint8_t* data = fifo.bytes;
    uint16_t pos = 0;
    while (pos < len) {
        const uint8_t id = data[pos];
        const uint8_t size = event_bytes(id);
        if (size == 0) {
            unparsed_++;
            fifo.resync = true;
            return len;
        }
        if (pos + size > len) break;
        if (id == kSensorAccelerometer) {
            sample_.x_mg = to_milli_g(data + pos + 1);
            sample_.y_mg = to_milli_g(data + pos + 3);
            sample_.z_mg = to_milli_g(data + pos + 5);
            sample_.at_ms = now_ms;
            fresh_ = true;
        } else if (id == kSysIdMetaEvent || id == kSysIdMetaEventWakeup) {
            note_meta_event(data + pos + 1);
        }
        pos = static_cast<uint16_t>(pos + size);
    }
    return pos;
}

void Bhi260::note_meta_event(const uint8_t* event) {
    meta_event_ = event[0];
    if (event[0] != kMetaEventSensorError) return;
    errored_sensor_ = event[1];
    sensor_error_ = event[2];
}

uint8_t Bhi260::event_bytes(uint8_t id) {
    if (id == kSysIdPadding) return 1;
    if (id == kSensorAccelerometer) return kAccelEventBytes;
    if (id >= kSysIdFirst) return kSysEventBytes[id - kSysIdFirst];
    return 0;
}

int16_t Bhi260::to_milli_g(const uint8_t* le16_bytes) {
    const int32_t raw = static_cast<int16_t>(le16(le16_bytes));
    return static_cast<int16_t>(raw * kRangeG * 1000 / kCountsPerRange);
}

bool Bhi260::write_registers(uint8_t reg, const uint8_t* data, uint16_t len) {
    frame_[0] = reg;
    for (uint16_t i = 0; i < len; i++) frame_[1 + i] = data[i];
    return bus_.write(address_, frame_, static_cast<size_t>(len) + 1);
}

bool Bhi260::read_registers(uint8_t reg, uint8_t* out, uint16_t len) {
    if (!bus_.write(address_, &reg, 1)) return false;
    return bus_.read(address_, out, len);
}

bool Bhi260::command(uint16_t cmd, const uint8_t* payload, uint16_t len) {
    uint16_t n = 0;
    frame_[n++] = kRegCommand;
    frame_[n++] = static_cast<uint8_t>(cmd & 0xFF);
    frame_[n++] = static_cast<uint8_t>(cmd >> 8);
    frame_[n++] = static_cast<uint8_t>(len & 0xFF);
    frame_[n++] = static_cast<uint8_t>(len >> 8);
    for (uint16_t i = 0; i < len; i++) frame_[n++] = payload[i];
    while ((n - 1) % 4 != 0) frame_[n++] = 0;
    return bus_.write(address_, frame_, n);
}

bool Bhi260::boot_status(uint8_t& out) {
    if (read_registers(kRegBootStatus, &out, 1)) return true;
    fail(Status::Down);
    return false;
}

void Bhi260::fail(Status why) {
    failed_stage_ = stage_;
    stage_ = Stage::Failed;
    fault_ = why;
}

bool LinkedImage::read(uint32_t offset, uint8_t* out, uint16_t len) {
    if (offset > bytes_.size() || len > bytes_.size() - offset) return false;
    for (uint16_t i = 0; i < len; i++) out[i] = bytes_[offset + i];
    return true;
}

}  // namespace skyblip::parts
