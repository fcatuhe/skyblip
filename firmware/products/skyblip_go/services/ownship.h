#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_OWNSHIP_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_SERVICES_OWNSHIP_H

#include "core/events/sensor.h"
#include "core/flight/atmosphere.h"
#include "core/flight/extrapolate.h"
#include "core/flight/force.h"
#include "core/flight/gload.h"
#include "core/flight/ground.h"
#include "core/flight/slip.h"
#include "core/flight/state.h"
#include "core/flight/timer.h"
#include "core/gnss/acquisition.h"
#include "core/gnss/first_fix.h"
#include "core/model/ownship.h"
#include "products/skyblip_go/settings.h"
#include "runtime/service.h"
#include "runtime/tasks.h"

namespace skyblip::go {

// Owns state.own: sensor readings become the one state the protocol encoder, the
// alarm logic and every screen read.
class OwnshipService : public runtime::Service {
   public:
    OwnshipService(runtime::Context& context, const Settings& settings)
        : runtime::Service(context), settings_(settings) {}

    void tick(uint32_t now_ms) override;

    bool baro_active() const { return baro_live_; }

    // ADS-L G.1.2 FlightState, decided by core/flight from the fix stream.
    flight::FlightState flight_state_from(const model::OwnState& own, uint32_t now_ms);

    // The one copy of "has the receiver settled": the transmit gate reads it
    // through state.own.tx_settled, and whoever annunciates the first fix takes
    // the edge from here rather than keeping a second watch of its own.
    const gnss::FirstFix& first_fix() const { return settle_; }
    bool take_fix_acquired() { return settle_.take_acquired(); }

   private:
    void apply_solution(const gnss::GnssSolution& solution, uint32_t now_ms);
    void record_gnss(const gnss::GnssSolution& solution, uint32_t now_ms);
    void record_flight(uint32_t now_ms);
    void record_pps(uint32_t now_ms);
    void record_baro(const events::BaroSample& sample, int32_t alt_mm, int32_t climb_mm_s,
                     bool adopted, uint32_t now_ms);
    void record_motion(uint32_t now_ms);
    uint32_t solution_instant(const gnss::GnssSolution& solution, uint32_t now_ms) const;
    void publish_solution_phase(uint32_t now_ms);
    void anchor_utc(const gnss::GnssSolution& solution);
    void apply_baro(const events::BaroSample& sample, uint32_t now_ms);
    void apply_accel(const events::AccelSample& sample);
    void publish_inertial(uint32_t now_ms);
    void update_turn_rate(uint32_t now_ms);
    void update_residual(const model::OwnState& previous);
    static gnss::Convergence convergence_of(const model::OwnState& own);
    static bool height_solved(const model::OwnState& own);
    void adopt_climb(int32_t mm_s);
    static bool vs_from_alt_mm(int32_t alt_mm, uint32_t now_ms, uint32_t window_ms,
                               int32_t& ref_alt_mm, uint32_t& ref_ms, int32_t& out_mm_s);
    bool baro_heard_within_max_age(uint32_t now_ms) const;

    flight::SlipBall ball_{};
    flight::GMeter gmeter_{};
    bool flying_{false};
    flight::FlightMonitor flight_{};
    flight::FlightTimer timer_{};
    flight::GroundLatch ground_{};
    gnss::FirstFix settle_{};
    gnss::Acquisition acquisition_{};
    int32_t vs_ref_alt_mm_{0};
    uint32_t vs_ref_ms_{0};
    int32_t baro_ref_alt_mm_{0};
    uint32_t baro_ref_ms_{0};
    uint32_t baro_heard_ms_{0};
    bool baro_live_{false};
    uint32_t turn_ref_ms_{0};
    int32_t turn_ref_track_cdeg_{0};
    uint64_t pps_edge_us_{0};
    uint32_t pps_recorded_ms_{0};
    uint32_t motion_recorded_ms_{0};

    static constexpr uint32_t kBaroVsWindowMs = flight::kMinWindowMs;
    static constexpr uint32_t kGnssVsWindowMs = 2000;
    // INFO: fc 23sep26 three missed samples: a barometer that went silent hands the climb back
    static constexpr uint32_t kBaroMaxAgeMs = 3 * runtime::kBaroPeriodMs;
    // INFO: fc 20sep26 an edge is a record, and a second that brought none is the record saying so
    static constexpr uint32_t kPpsRecordPeriodMs = 1000;
    // INFO: fc 20sep26 the hub reports faster than the filters behind it move, in whole seconds
    static constexpr uint32_t kMotionRecordPeriodMs = 1000;
    const Settings& settings_;
};

}  // namespace skyblip::go

#endif
