#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CAPTURE_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_CAPTURE_H

#include "core/comms/config.h"
#include "core/diag/payload.h"
#include "core/diag/profile.h"
#include "products/skyblip_go/services/record_store.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"

namespace skyblip::go {

class CaptureService : public runtime::Service {
   public:
    // INFO: fc 20sep26 the price walks the whole pool, so it is answered at the render cadence
    static constexpr uint32_t kPublishPeriodMs = 1000;

    // INFO: fc 20sep26 one pass of slot programs, so the next dwell is armed on time
    static constexpr uint32_t kDrainCeilingRecords = 16;

    // INFO: fc 20sep26 the gap that says why it stopped, and the end marker that closes it
    static constexpr uint32_t kTailSlotsReserved = 2;

    CaptureService(runtime::Context& context, RecordStore& store, const RecordStore& flights,
                   const Settings& settings, const comms::ConfigService& config)
        : runtime::Service(context),
          store_(store),
          flights_(flights),
          settings_(settings),
          config_(config) {}

    Status setup() override;
    void tick(uint32_t now_ms) override;
    void park(uint32_t now_ms);

    bool available() const { return store_.available(); }
    bool capturing() const { return open_; }
    uint32_t session_id() const { return session_id_; }
    uint32_t records_written() const { return store_.session_records(); }
    uint32_t records_dropped() const { return context_.diag.dropped(); }
    uint32_t sectors_owned() const { return store_.sectors_owned(); }
    bus::CaptureStop stopped() const { return stopped_; }

    uint32_t keeps_s(diag::Profile profile) const;

   private:
    void open(uint32_t now_ms);
    void record_boot(uint32_t now_ms);
    void record_config(uint32_t now_ms);
    bool close(uint32_t now_ms);
    bool write_end(uint32_t now_ms);
    void drain(uint32_t now_ms);
    bool announce_rotation(uint32_t now_ms);
    void stop_on_refusal(uint32_t now_ms);
    bool write_gap(uint32_t now_ms);
    void publish(uint32_t now_ms);
    bool publish_due(uint32_t now_ms) const;
    void count_toward_rate(diag::Type type, uint32_t now_ms);
    uint32_t records_per_hour() const;
    uint32_t measured_records_per_hour() const;
    uint32_t growth_slots() const;
    uint32_t growth_sectors() const;
    static uint32_t span_s(uint32_t slots, uint32_t records_per_hour);

    RecordStore& store_;
    const RecordStore& flights_;
    const Settings& settings_;
    const comms::ConfigService& config_;
    uint32_t session_id_{0};
    uint32_t opened_ms_{0};
    uint32_t steady_records_{0};
    uint32_t steady_written_ms_{0};
    uint32_t published_ms_{0};
    uint32_t lost_records_{0};
    bool published_{false};
    bus::CaptureStop stopped_{bus::CaptureStop::None};
    bool open_{false};
    bool ended_{false};

    uint8_t scratch_[kStoreRecordBytes]{};
};

}  // namespace skyblip::go

#endif
