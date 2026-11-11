#ifndef SKYBLIP_HARDWARE_PLATFORM_HOST_CENTRAL_H
#define SKYBLIP_HARDWARE_PLATFORM_HOST_CENTRAL_H

#include <cstring>
#include <deque>
#include <optional>
#include <string>

#include "core/bus/bus.h"
#include "core/events/central.h"
#include "core/events/link.h"
#include "ports/central.h"

namespace skyblip::platform::host {

// INFO: fc 11nov26 the Go's central on the host: the case, or a PairedLink, plays the air
class Central : public ports::Central {
   public:
    Status scan(const ports::ScanDuty& duty, const char* name_prefix) override {
        scan_duty_ = duty;
        name_prefix_ = name_prefix;
        scans++;
        return Status::Ok;
    }
    void stop_scan() override { scan_duty_.reset(); }

    Status connect(const events::BluetoothAddress& address, const ports::ScanDuty& duty) override {
        if (scan_duty_ || wanted_ || up_) return Status::WouldBlock;
        wanted_ = address;
        connect_duty_ = duty;
        connects++;
        return Status::Ok;
    }

    void disconnect() override {
        if (!wanted_ && !up_) return;
        drop();
    }

    Status write(ConstByteSpan bytes) override {
        if (!up_ || bytes.size() > payload_bytes_) {
            writes_refused++;
            return up_ ? Status::OutOfRange : Status::Down;
        }
        writes.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return Status::Ok;
    }

    bool pop_event(events::CentralEvent& out) { return events_.pop(out); }
    bool pop_rx(events::RxFrame& out) { return rx_.pop(out); }

    bool scanning() const { return scan_duty_.has_value(); }
    const std::optional<ports::ScanDuty>& scan_duty() const { return scan_duty_; }
    bool connecting() const { return wanted_.has_value() && !up_; }
    bool up() const { return up_; }
    const std::optional<events::BluetoothAddress>& wanted() const { return wanted_; }
    const ports::ScanDuty& connect_duty() const { return connect_duty_; }
    uint16_t payload_bytes() const { return payload_bytes_; }

    void hear(const events::BluetoothAddress& address, const char* name) {
        if (!scanning() || std::strncmp(name, name_prefix_.c_str(), name_prefix_.size()) != 0)
            return;
        events::CentralEvent event{};
        event.type = events::CentralEventType::Found;
        event.address = address;
        std::strncpy(event.name, name, events::kAdvertisedNameBytes);
        events_.push(event);
    }

    void come_up(uint16_t payload_bytes) {
        if (!wanted_ || up_) return;
        up_ = true;
        payload_bytes_ = payload_bytes;
        events::CentralEvent event{};
        event.type = events::CentralEventType::Up;
        event.address = wanted_.value();
        event.payload_bytes = payload_bytes;
        events_.push(event);
    }

    // INFO: fc 11nov26 lost, refused or never made, as Zephyr's connected() with an error
    void drop() {
        events::CentralEvent event{};
        event.type = events::CentralEventType::Down;
        if (wanted_) event.address = *wanted_;
        wanted_.reset();
        up_ = false;
        events_.push(event);
    }

    bool notify(const std::string& bytes) {
        if (!up_) return false;
        events::RxFrame frame{};
        frame.endpoint = events::Endpoint::Peer;
        frame.bytes = static_cast<uint16_t>(bytes.size());
        std::memcpy(frame.data.data(), bytes.data(), bytes.size());
        return rx_.push(frame);
    }

    std::deque<std::string> writes{};
    int scans{0};
    int connects{0};
    int writes_refused{0};

   private:
    bus::Queue<events::CentralEvent, 8> events_;
    bus::Queue<events::RxFrame, 8> rx_;
    std::optional<ports::ScanDuty> scan_duty_{};
    std::string name_prefix_{};
    std::optional<events::BluetoothAddress> wanted_{};
    ports::ScanDuty connect_duty_{};
    uint16_t payload_bytes_{0};
    bool up_{false};
};

}  // namespace skyblip::platform::host

#endif
