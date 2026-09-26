#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_FLIGHT_LOG_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_FLIGHT_LOG_H

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/diag/payload.h"
#include "core/flight/log_session.h"
#include "products/skyblip_go/services/record_store.h"
#include "runtime/service.h"

namespace skyblip::go {

class FlightLogService : public runtime::Service {
   public:
    FlightLogService(runtime::Context& context, RecordStore& flights, RecordStore* diagnostics,
                     comms::ConfigService& config)
        : runtime::Service(context),
          flights_(flights),
          diagnostics_(diagnostics),
          config_(config) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;

    bool available() const { return flights_.available(); }
    bool recording() const { return session_.open(); }
    uint32_t session_id() const { return session_.session_id(); }
    uint32_t records_written() const { return flights_.records_written(); }
    uint32_t records_dropped() const { return session_.dropped(); }
    uint32_t sessions_on_flash() const { return flights_.sessions(); }
    uint32_t sectors_owned() const { return flights_.sectors_owned(); }
    bool erasing() const { return flights_.erasing(); }
    const flight::LogRing& ring() const { return flights_.ring(); }
    uint32_t recovery_bytes_read() const { return flights_.pool().scan_bytes_read(); }
    uint32_t link_drops() const { return flights_.pool().link_drops(); }

   private:
    void drain(uint32_t now_ms);
    void serve_link(uint32_t now_ms);
    void abandon_reads();
    // While true the next command waits on the bus: its answer would have nowhere to go.
    bool replying() const;
    void handle(const comms::LogRequest& request);
    void record_link(diag::LinkAction action, uint16_t session, uint16_t frame_bytes,
                     uint32_t now_ms);
    void watch_link_drops(uint32_t now_ms);
    RecordStore* store_for(comms::LogStore store);
    void ack(comms::LogStore store, bool ok, const char* reason);
    bool on_ground() const;

    RecordStore& flights_;
    RecordStore* const diagnostics_;
    comms::ConfigService& config_;
    flight::LogSession session_{};
    uint32_t recorded_drops_{0};
    uint16_t reply_to_{0};

    uint8_t scratch_[kStoreRecordBytes]{};
};

}  // namespace skyblip::go

#endif
