#ifndef SKYBLIP_CORE_COMMS_CONFIG_H
#define SKYBLIP_CORE_COMMS_CONFIG_H

#include <cstdint>
#include <optional>

#include "core/comms/diagnostics.h"
#include "core/comms/link_claim.h"
#include "core/comms/link_registry.h"
#include "core/comms/timing_report.h"
#include "core/events/link.h"
#include "core/timing/slot_timing_statistics.h"
#include "core/util/result.h"
#include "ports/link.h"

namespace skyblip::comms {

// INFO: fc 04aug26 an iPhone's usual ATT_MTU 185 less the header: replies a pilot needs fit it
constexpr int kSmallestSupportedPayloadBytes = 182;

class ConfigChannel {
   public:
    static constexpr int kHeldFrameBytes = kTimingFrameBytes;

    ConfigChannel(ports::Link& link, uint32_t& link_drops) : link_(link), link_drops_(link_drops) {}

    void tick(uint32_t now_ms) { now_ms_ = now_ms; }
    uint32_t now_ms() const { return now_ms_; }
    // INFO: fc 25sep26 a reply the link refused with WouldBlock goes out here, and a report resumes
    void resume(uint32_t now_ms);
    bool replying() const { return held_bytes_ > 0 || diagnostics_report_ || timing_report_; }

    bool disconnect(const events::Peer& peer);
    bool release_departed_holder();
    // INFO: fc 27nov26 read from the link, because a full link_events queue drops an Up
    bool link_up() const { return link_.centrals() > 0; }
    bool live(const events::Peer& peer) const {
        return link_.connection(peer.session_id) == peer.connection;
    }
    // INFO: fc 10nov26 a frame from a connection that dropped is nobody's command
    bool from_live_connection(const events::RxFrame& frame) const {
        return frame.connection != 0 && live(events::peer_of(frame));
    }

    bool admit(const events::Peer& peer);
    LinkClaim& claim() { return claim_; }
    const LinkClaim& claim() const { return claim_; }

    int payload_bytes() const { return static_cast<int>(link_.payload_bytes()); }
    int payload_bytes_to(uint16_t session_id) const {
        return static_cast<int>(link_.payload_bytes_to(session_id));
    }

    Status reply(const uint8_t* frame, int bytes);
    Status reply_to(const events::Peer& peer, const uint8_t* frame, int bytes);
    Status deliver(const events::Peer& peer, const uint8_t* frame, int bytes);
    Status broadcast(const uint8_t* frame, int bytes);
    void acknowledge(bool accepted, std::optional<link::Reason> reason = std::nullopt);
    void acknowledge_to(const events::Peer& peer, bool accepted,
                        std::optional<link::Reason> reason);
    void acknowledge_pending(link::Reason reason);
    void refuse_too_wide() { acknowledge(false, link::Reason::SmallMtu); }

    void start_diagnostics(const Diagnostics& diagnostics, link::Command command);
    void start_diagnostics(const Diagnostics& diagnostics, link::Command command,
                           DiagnosticsReport::Group only);
    void start_timing(const timing::SlotTimingStatistics& statistics);

   private:
    void refuse_unclaimed(const events::Peer& peer);
    void hold(const events::Peer& peer, const uint8_t* frame, int bytes);
    void drop_replies();
    template <class Report, class... Arguments>
    void start_report(std::optional<Report>& report, int frame_bytes,
                      const Arguments&... arguments);
    template <class Report>
    void continue_report(std::optional<Report>& report);

    static_assert(kHeldFrameBytes >= DiagnosticsReport::kFrameBytes,
                  "a report frame must be holdable");

    ports::Link& link_;
    uint32_t& link_drops_;
    LinkClaim claim_{};
    uint32_t now_ms_{0};
    uint8_t held_frame_[kHeldFrameBytes]{};
    int held_bytes_{0};
    events::Peer held_for_{0, 0};
    uint32_t held_since_ms_{0};
    std::optional<DiagnosticsReport> diagnostics_report_{};
    std::optional<TimingReport> timing_report_{};
    int report_payload_bytes_{0};
    int report_frame_bytes_{0};
};

}  // namespace skyblip::comms

#endif
