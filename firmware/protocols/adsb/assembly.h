#ifndef SKYBLIP_PROTOCOLS_ADSB_ASSEMBLY_H
#define SKYBLIP_PROTOCOLS_ADSB_ASSEMBLY_H

#include <array>
#include <cstdint>

#include "protocols/adsb/cpr.h"
#include "protocols/adsb/message.h"

namespace skyblip::adsb {

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

struct Reference {
    CprPosition position;
    bool valid{false};
};

// INFO: fc 13nov26 the receiver's own dating of a message, carried through to the track untouched
struct Receipt {
    uint32_t utc_s{0};
    uint16_t utc_ms{0};
    bool utc_valid{false};
};

enum class AirGround : uint8_t { Unknown, Ground, Airborne };

enum class MotionSource : uint8_t { None, Velocity, Surface };

struct Motion {
    int32_t speed_mkt{0};
    int16_t east_kt{0};
    int16_t north_kt{0};
    MotionSource source{MotionSource::None};
    uint8_t track_c7{0};
    bool track_valid{false};
};

// INFO: fc 13nov26 every age is how long before at_ms the field was true, negative after it
struct Track {
    uint32_t address{0};
    uint32_t at_ms{0};
    Receipt received;
    int8_t rssi_dbm{0};
    bool rssi_valid{false};
    bool position_valid{false};
    CprPosition position;
    bool identified{false};
    uint8_t emitter_type_code{0};
    uint8_t emitter_category{0};
    AirGround air_ground{AirGround::Unknown};
    int32_t air_ground_age_ms{0};
    Motion motion;
    int32_t motion_age_ms{0};
    bool climb_valid{false};
    int32_t climb_fpm{0};
    HeightSource climb_source{HeightSource::Gnss};
    int32_t climb_age_ms{0};
    bool pressure_altitude_valid{false};
    int32_t pressure_altitude_ft{0};
    int32_t pressure_altitude_age_ms{0};
    bool height_difference_valid{false};
    int32_t height_difference_ft{0};
    int32_t height_difference_age_ms{0};
    bool gnss_height_valid{false};
    int32_t gnss_height_ft{0};
    int32_t gnss_height_age_ms{0};
    bool version_valid{false};
    uint8_t version{0};
    bool gva_valid{false};
    uint8_t gva{0};
};

class Assembly {
   public:
    static constexpr int kMaxAircraft = 100;

    void set_own_address(uint32_t address) { own_address_ = address; }

    bool fold(const Message& message, uint32_t at_ms, const Receipt& received,
              const Reference& reference, Track& out);

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
        Cpr cpr;
        uint32_t at_ms{0};
        CprFormat format{CprFormat::Airborne};
        bool slow{false};
        bool held{false};
        bool founding{false};
    };

    struct Aircraft {
        uint32_t address{0};
        uint32_t heard_at_ms{0};
        Receipt received;
        CprPosition position;
        uint32_t position_at_ms{0};
        int32_t range_m{0};
        CprHalf even;
        CprHalf odd;
        Timed pressure_altitude_ft;
        Timed gnss_height_ft;
        Timed height_difference_ft;
        Timed climb_fpm;
        Timed air_ground;
        uint32_t motion_at_ms{0};
        Motion motion;
        int8_t rssi_dbm{0};
        bool rssi_valid{false};
        uint8_t emitter_type_code{0};
        uint8_t emitter_category{0};
        uint8_t version{0};
        uint8_t gva{0};
        HeightSource climb_source{HeightSource::Gnss};
        CprFormat position_format{CprFormat::Airborne};
        bool position_valid{false};
        bool confirmed{false};
        bool identified{false};
        bool version_valid{false};
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

    Aircraft* admit(const Message& message, uint32_t at_ms, const Reference& reference);
    void fold_position(Aircraft& aircraft, CprFormat format, const Cpr& cpr, bool slow,
                       uint32_t at_ms, const Receipt& received, const Reception& reception,
                       const Reference& reference);
    static bool confirm(Aircraft& aircraft, const CprHalf& newest, const CprPosition& local);
    static void fold_velocity(Aircraft& aircraft, const Velocity& velocity, uint32_t at_ms);
    static void fold_surface_motion(Aircraft& aircraft, const SurfacePosition& surface,
                                    uint32_t at_ms);
    static bool pairs(const CprHalf& newest, const CprHalf& other);
    static void arrive(Aircraft& aircraft, uint32_t address, uint32_t at_ms);
    static void expire(Aircraft& aircraft, uint32_t now_ms);
    static bool matters_less(const Weight& weight, const Weight& other);
    static Weight weight_of(const Aircraft& aircraft, uint32_t at_ms, bool ranked, bool referenced);
    static int32_t range_now(const Aircraft& aircraft, const Reference& reference);
    static int32_t range_of(const Message& message, const Reference& reference);
    static int32_t tentative_range_m(CprFormat format, const Cpr& cpr, const Reference& reference);
    static void observe(const Aircraft& aircraft, Track& out);
};

}

#endif
