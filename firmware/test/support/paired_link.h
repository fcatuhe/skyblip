// Harness, not a test: a paired Go and Bravo's Bluetooth link, its faults injected by the case.
#ifndef SKYBLIP_TEST_SUPPORT_PAIRED_LINK_H
#define SKYBLIP_TEST_SUPPORT_PAIRED_LINK_H

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>

#include "core/events/central.h"
#include "core/events/link.h"
#include "core/util/result.h"
#include "hardware/platform/host/central.h"
#include "hardware/platform/host/link.h"

namespace skyblip {

class PairedLink {
   public:
    enum class Side : uint8_t { Central, Peripheral };
    enum class Direction : uint8_t { ToCentral, ToPeripheral };
    enum class Flow : uint8_t { Flowing, Stalled, Lost };

    // INFO: fc 11nov26 tag 3F2A, a static random address as an nRF52 advertises with its FICR's
    static constexpr events::BluetoothAddress kPeripheralAddress{
        1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};

    explicit PairedLink(platform::host::Link& peripheral, uint16_t session_id = 1)
        : peripheral_(&peripheral), session_id_(session_id) {
        peripheral_->tap(sent_);
    }

    // INFO: fc 11nov26 a Go's central drives this one: it scans, connects, writes and reads
    PairedLink(platform::host::Link& peripheral, platform::host::Central& central,
               uint16_t session_id = 1)
        : PairedLink(peripheral, session_id) {
        central_ = &central;
    }
    ~PairedLink() { peripheral_->untap(sent_); }
    PairedLink(const PairedLink&) = delete;
    PairedLink& operator=(const PairedLink&) = delete;
    PairedLink(PairedLink&&) = delete;
    PairedLink& operator=(PairedLink&&) = delete;

    bool connect() {
        if (central_up_ || peripheral_up_) return false;
        collect_notifications();
        peripheral_->raise_link(session_id_);
        if (peripheral_->connection(session_id_) == 0) return false;
        central_up_ = true;
        peripheral_up_ = true;
        payload_bytes_ = peripheral_->payload_bytes_to(session_id_);
        return true;
    }

    // INFO: fc 10nov26 a half the other side's reboot left up drops here, at supervision timeout
    void drop() {
        collect_notifications();
        if (peripheral_up_) peripheral_->drop_link(session_id_);
        if (central_ != nullptr && central_up_) central_->drop();
        central_up_ = false;
        peripheral_up_ = false;
        lose_held();
    }

    void reboot_central() {
        collect_notifications();
        central_up_ = false;
        inbox_.clear();
        lose_held();
    }

    void reboot_central(platform::host::Central& rebooted) {
        reboot_central();
        central_ = &rebooted;
    }

    // INFO: fc 11nov26 out of range or off: no scan hears the peripheral, no connect takes it
    void set_in_range(bool in_range) { in_range_ = in_range; }

    // INFO: fc 10nov26 a rebooted peripheral brings a new link, numbering connections from 1
    void reboot_peripheral(platform::host::Link& rebooted) {
        collect_notifications();
        peripheral_->untap(sent_);
        peripheral_ = &rebooted;
        peripheral_->tap(sent_);
        peripheral_up_ = false;
        lose_held();
    }

    void set_flow(Direction direction, Flow flow) { pipe(direction).flow = flow; }
    void set_flow(Flow flow) {
        set_flow(Direction::ToCentral, flow);
        set_flow(Direction::ToPeripheral, flow);
    }

    void pump() {
        if (central_ != nullptr) serve_central();
        collect_notifications();
        carry(to_central_, [this](const std::string& bytes) {
            if (central_ != nullptr) return central_->notify(bytes);
            inbox_.push_back(bytes);
            return true;
        });
        carry(to_peripheral_,
              [this](const std::string& bytes) { return push_to_peripheral(bytes); });
    }

    Status write(const std::string& bytes) {
        if (!central_up_) return Status::Down;
        if (peripheral_up_) payload_bytes_ = peripheral_->payload_bytes_to(session_id_);
        if (bytes.size() > payload_bytes_ || bytes.size() > sizeof(events::RxFrame::data))
            return Status::OutOfRange;
        to_peripheral_.held.push_back(bytes);
        return Status::Ok;
    }

    bool take(std::string& out) {
        if (inbox_.empty()) return false;
        out = inbox_.front();
        inbox_.pop_front();
        return true;
    }

    bool up(Side side) const { return side == Side::Central ? central_up_ : peripheral_up_; }
    int delivered(Direction direction) const { return pipe(direction).delivered; }
    int lost(Direction direction) const { return pipe(direction).lost; }

   private:
    struct Pipe {
        Flow flow{Flow::Flowing};
        std::deque<std::string> held;
        int delivered{0};
        int lost{0};
    };

    Pipe& pipe(Direction direction) {
        return direction == Direction::ToCentral ? to_central_ : to_peripheral_;
    }
    const Pipe& pipe(Direction direction) const {
        return direction == Direction::ToCentral ? to_central_ : to_peripheral_;
    }

    // INFO: fc 11nov26 a central that disconnects tells the peripheral at once, unlike a reboot
    void serve_central() {
        if (central_up_ && !central_->up()) {
            collect_notifications();
            if (peripheral_up_) peripheral_->drop_link(session_id_);
            central_up_ = false;
            peripheral_up_ = false;
            lose_held();
        }
        if (!central_->scanning()) heard_ = false;
        if (central_->scanning() && in_range_ && !heard_ && !peripheral_->advertised.empty()) {
            central_->hear(kPeripheralAddress, peripheral_->advertised.c_str());
            heard_ = true;
        }
        if (central_->connecting() && central_->wanted() == kPeripheralAddress && in_range_ &&
            connect())
            central_->come_up(payload_bytes_);
        for (; !central_->writes.empty(); central_->writes.pop_front()) {
            if (central_up_)
                to_peripheral_.held.push_back(central_->writes.front());
            else
                to_peripheral_.lost++;
        }
    }

    void collect_notifications() {
        for (; !sent_.empty(); sent_.pop_front()) {
            const platform::host::Link::Frame& frame = sent_.front();
            const bool ours = frame.endpoint == events::Endpoint::Peer &&
                              (frame.session == 0 || frame.session == session_id_);
            if (!ours) continue;
            if (central_up_ && peripheral_up_)
                to_central_.held.push_back(frame.bytes);
            else
                to_central_.lost++;
        }
    }

    template <typename Deliver>
    void carry(Pipe& pipe, Deliver deliver) {
        if (!central_up_ || !peripheral_up_ || pipe.flow == Flow::Lost) {
            pipe.lost += static_cast<int>(pipe.held.size());
            pipe.held.clear();
            return;
        }
        if (pipe.flow == Flow::Stalled) return;
        while (!pipe.held.empty() && deliver(pipe.held.front())) {
            pipe.held.pop_front();
            pipe.delivered++;
        }
    }

    // INFO: fc 10nov26 a write the full queue refuses waits, as a central retries it
    bool push_to_peripheral(const std::string& bytes) {
        events::RxFrame frame{};
        frame.session_id = session_id_;
        frame.endpoint = events::Endpoint::Peer;
        frame.bytes = static_cast<uint16_t>(bytes.size());
        std::memcpy(frame.data.data(), bytes.data(), bytes.size());
        return peripheral_->push_rx(frame);
    }

    void lose_held() {
        for (Pipe* pipe : {&to_central_, &to_peripheral_}) {
            pipe->lost += static_cast<int>(pipe->held.size());
            pipe->held.clear();
        }
    }

    platform::host::Link* peripheral_;
    platform::host::Central* central_{nullptr};
    uint16_t session_id_;
    bool in_range_{true};
    bool heard_{false};
    uint16_t payload_bytes_{ports::kMinimumLinkPayloadBytes};
    bool central_up_{false};
    bool peripheral_up_{false};
    Pipe to_central_{};
    Pipe to_peripheral_{};
    std::deque<std::string> inbox_{};
    std::deque<platform::host::Link::Frame> sent_{};
};

}  // namespace skyblip

#endif
