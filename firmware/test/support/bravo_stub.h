// Harness, not a test: Bravo on the peer endpoint, a recording replayed for a Go case.
#ifndef SKYBLIP_TEST_SUPPORT_BRAVO_STUB_H
#define SKYBLIP_TEST_SUPPORT_BRAVO_STUB_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/events/link.h"
#include "hardware/platform/host/link.h"
#include "protocols/common/test/channel.h"
#include "test/support/config_frame.h"
#include "test/support/registry_frames.h"

namespace skyblip {

class BravoStub {
   public:
    // INFO: fc 10nov26 schemas/link.v1.json, health: once a second whatever the traffic
    static constexpr uint32_t kHealthPeriodMs = 1000;
    static constexpr uint32_t kProtocolVersion = 1;
    // INFO: fc 11nov26 the name Bravo must advertise (products/common/README.md), tag 3F2A
    static constexpr const char* kAdvertisedName = "skyBlip Bravo 3F2A";

    struct Recorded {
        uint32_t at_ms;
        std::string observation_fields;
    };

    struct Health {
        uint32_t protocol_version{kProtocolVersion};
        bool listening{true};
        bool utc_valid{true};
        bool own_icao_set{false};
        std::string export_reference{"own"};
        bool paired{true};
        std::string own_transponder_fields{};
    };

    explicit BravoStub(std::vector<Recorded> recording, uint32_t seed = 1)
        : recording_(std::move(recording)), random_(seed) {
        boot();
    }

    platform::host::Link& link() { return *link_; }

    platform::host::Link& reboot() {
        boot();
        return *link_;
    }

    void run_pass(uint32_t now_ms) {
        read_claims();
        events::LinkEvent event{};
        while (link_->pop_event(event)) {
        }
        if (!booted_) {
            booted_ = true;
            next_health_at_ms_ = now_ms;
        }
        serve_recording(now_ms);
        if (static_cast<int32_t>(now_ms - next_health_at_ms_) >= 0) {
            next_health_at_ms_ +=
                (now_ms - next_health_at_ms_) / kHealthPeriodMs * kHealthPeriodMs + kHealthPeriodMs;
            if (link_->up()) notify(health_frame());
        }
    }

    uint32_t boot_epoch() const { return boot_epoch_; }

    Health health{};
    std::vector<std::string> claims{};
    int refused{0};

   private:
    void boot() {
        previous_link_ = std::move(link_);
        link_ = std::make_unique<platform::host::Link>();
        (void)link_->advertise_as(kAdvertisedName);
        boot_epoch_ = random_.next();
        sequence_ = 0;
        sent_observation_ = false;
        booted_ = false;
        claims.clear();
    }

    void read_claims() {
        events::RxFrame frame{};
        while (link_->pop_rx(frame))
            if (frame.endpoint == events::Endpoint::Peer)
                claims.push_back(link_text(
                    std::string(reinterpret_cast<const char*>(frame.data.data()), frame.bytes)));
    }

    void serve_recording(uint32_t now_ms) {
        while (next_recorded_ < recording_.size() &&
               static_cast<int32_t>(now_ms - recording_[next_recorded_].at_ms) >= 0) {
            if (link_->up() && exporting()) {
                notify(observation_frame(recording_[next_recorded_].observation_fields));
                sent_observation_ = true;
                sequence_++;
            }
            next_recorded_++;
        }
    }

    bool exporting() const { return health.utc_valid && health.export_reference != "none"; }

    std::string observation_frame(const std::string& fields) const {
        return complete_registry_frame(
            "{\"command\":\"observation\",\"boot_epoch\":" + std::to_string(boot_epoch_) +
            ",\"sequence\":" + std::to_string(sequence_) + "," + fields + "}");
    }

    std::string health_frame() const {
        std::string json = "{\"command\":\"health\",\"protocol_version\":" +
                           std::to_string(health.protocol_version) +
                           ",\"boot_epoch\":" + std::to_string(boot_epoch_) +
                           ",\"listening\":" + text_of(health.listening) +
                           ",\"utc_valid\":" + text_of(health.utc_valid) +
                           ",\"own_icao_set\":" + text_of(health.own_icao_set) +
                           ",\"alarm_granted\":false,\"export_reference\":\"" +
                           health.export_reference + "\",\"paired\":" + text_of(health.paired);
        if (sent_observation_) json += ",\"last_sequence\":" + std::to_string(sequence_ - 1);
        if (!health.own_transponder_fields.empty()) json += "," + health.own_transponder_fields;
        return complete_registry_frame(json + "}");
    }

    void notify(const std::string& frame) {
        const Status sent = link_->send(
            events::Endpoint::Peer,
            ConstByteSpan(reinterpret_cast<const uint8_t*>(frame.data()), frame.size()));
        if (sent != Status::Ok) refused++;
    }

    static std::string text_of(bool value) { return value ? "true" : "false"; }

    std::vector<Recorded> recording_;
    size_t next_recorded_{0};
    wire::Random random_;
    std::unique_ptr<platform::host::Link> link_{};
    // INFO: fc 10nov26 kept to the next reboot, so a link bound to it counts what it lost
    std::unique_ptr<platform::host::Link> previous_link_{};
    uint32_t boot_epoch_{0};
    uint32_t sequence_{0};
    uint32_t next_health_at_ms_{0};
    bool sent_observation_{false};
    bool booted_{false};
};

}  // namespace skyblip

#endif
