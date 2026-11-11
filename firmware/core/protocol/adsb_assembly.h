#ifndef SKYBLIP_CORE_PROTOCOL_ADSB_ASSEMBLY_H
#define SKYBLIP_CORE_PROTOCOL_ADSB_ASSEMBLY_H

#include <array>
#include <cstdint>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/protocol/adsb_cpr.h"

namespace skyblip::protocol {

// INFO: fc 10nov26 ADS-L F.2.2 numbers the ICAO table 5, and a DF17 address is an ICAO address
constexpr uint8_t kIcaoAddressMappingTable = 5;

constexpr uint32_t kAirbornePairMaxAgeMs = 10000;
constexpr uint32_t kSlowSurfacePairMaxAgeMs = 50000;
constexpr uint32_t kSurfacePairMaxAgeMs = 25000;
constexpr int32_t kSlowSurfaceSpeedCeilingMkt = 25000;
constexpr uint32_t kTrackPositionMaxAgeMs = 120000;
constexpr uint32_t kTrackJumpWindowMs = 30000;
constexpr int32_t kAirborneJumpCeilingM = 6 * 1852;
constexpr int32_t kTransitionJumpCeilingM = 5 * 1852 / 2;
constexpr int32_t kSurfaceJumpCeilingM = 3 * 1852 / 4;
constexpr int32_t kAdsbRangeCeilingM = 180 * 1852;
constexpr int64_t kAirborneAgreementCeilingMm = 5000;
constexpr int64_t kSurfaceAgreementCeilingMm = 1250;
constexpr uint32_t kHeightDifferenceMaxAgeMs = 10000;
constexpr uint32_t kAdsbAircraftForgetMs = 250000;

struct AdsbReference {
    CprPosition position;
    bool valid{false};
};

class AdsbAssembly {
   public:
    static constexpr int kMaxAircraft = 100;

    void set_own_address(uint32_t address) { own_address_ = address; }

    bool fold(const AdsbMessage& message, uint32_t at_ms, const events::Stamp& received,
              const AdsbReference& reference, model::AircraftObservation& out);

    void age_out(uint32_t now_ms);
    int count() const;
    uint32_t evictions() const { return evictions_; }
    uint32_t refusals() const { return refusals_; }
    uint32_t implausible() const { return implausible_; }

   private:
    struct Timed {
        int32_t value{0};
        uint32_t at_ms{0};
        bool valid{false};
    };

    struct CprHalf {
        AdsbCpr cpr;
        uint32_t at_ms{0};
        CprFormat format{CprFormat::Airborne};
        bool slow{false};
        bool held{false};
        bool founding{false};
    };

    struct Aircraft {
        uint32_t address{0};
        uint32_t heard_at_ms{0};
        events::Stamp received;
        CprPosition position;
        uint32_t position_at_ms{0};
        int32_t range_m{0};
        CprHalf even;
        CprHalf odd;
        Timed pressure_altitude_ft;
        Timed gnss_height_ft;
        Timed height_difference_ft;
        Timed climb_e8;
        Timed flight_state;
        uint32_t speed_at_ms{0};
        uint16_t speed_q{0};
        uint16_t track_c9{0};
        int8_t rssi_dbm{0};
        bool rssi_valid{false};
        uint8_t aircraft_category{0};
        uint8_t adsb_version{0};
        uint8_t gva{0};
        model::ClimbSource climb_source{model::ClimbSource::Unknown};
        CprFormat position_format{CprFormat::Airborne};
        bool position_valid{false};
        bool confirmed{false};
        bool speed_valid{false};
        bool adsb_version_valid{false};
        bool gva_valid{false};
        bool range_current{false};
        bool used{false};
    };

    struct Weight {
        bool forgotten;
        int32_t range_m;
        uint32_t silent_ms;
    };

    std::array<Aircraft, kMaxAircraft> aircraft_{};
    uint32_t own_address_{0};
    uint32_t evictions_{0};
    uint32_t refusals_{0};
    uint32_t implausible_{0};
    CprPosition ranged_from_;
    bool ranged_{false};

    Aircraft* admit(const AdsbMessage& message, uint32_t at_ms, const AdsbReference& reference);
    void fold_position(Aircraft& aircraft, CprFormat format, const AdsbCpr& cpr, bool slow,
                       uint32_t at_ms, const events::Stamp& received,
                       const AdsbReception& reception, const AdsbReference& reference);
    static bool confirm(Aircraft& aircraft, const CprHalf& newest, const CprPosition& local);
    static void fold_velocity(Aircraft& aircraft, const AdsbVelocity& velocity, uint32_t at_ms);
    static void fold_surface_motion(Aircraft& aircraft, const AdsbSurfacePosition& surface,
                                    uint32_t at_ms);
    static bool pairs(const CprHalf& newest, const CprHalf& other);
    static void arrive(Aircraft& aircraft, uint32_t address, uint32_t at_ms);
    static void expire(Aircraft& aircraft, uint32_t now_ms);
    static bool matters_less(const Weight& weight, const Weight& other);
    static Weight weight_of(const Aircraft& aircraft, uint32_t at_ms, bool ranked, bool referenced);
    static int32_t range_now(const Aircraft& aircraft, const AdsbReference& reference);
    static int32_t range_of(const AdsbMessage& message, const AdsbReference& reference);
    static int32_t tentative_range_m(CprFormat format, const AdsbCpr& cpr,
                                     const AdsbReference& reference);
    static void observe(const Aircraft& aircraft, model::AircraftObservation& out);
};

}

#endif
