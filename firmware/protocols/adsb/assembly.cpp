#include "protocols/adsb/assembly.h"

#include <cstdint>
#include <initializer_list>

#include "protocols/common/geometry.h"

namespace skyblip::adsb {

using wire::cosine;
using wire::hypotenuse;
using wire::kMicrometresPerE7;
using wire::kTrigOne;
using wire::wrapped_longitude_1e7;

namespace {

constexpr uint8_t kGroundCapability = 4;
constexpr uint8_t kAirborneCapability = 5;
constexpr int32_t kUnknownRangeM = INT32_MAX;
constexpr int64_t kMicrometresPerMillimetre = 1000;
// INFO: fc 10nov26 0.1 degree is 194 m of longitude even at 89 degrees, far past any agreement
constexpr int64_t kFarApart1e7 = 1000000;

int32_t elapsed_ms(uint32_t later_ms, uint32_t earlier_ms) {
    return static_cast<int32_t>(later_ms - earlier_ms);
}

uint32_t since_ms(uint32_t now_ms, uint32_t then_ms) {
    const int32_t gap_ms = elapsed_ms(now_ms, then_ms);
    return gap_ms > 0 ? static_cast<uint32_t>(gap_ms) : 0;
}

bool same_place(const CprPosition& one, const CprPosition& other) {
    return one.latitude_1e7 == other.latitude_1e7 && one.longitude_1e7 == other.longitude_1e7;
}

uint32_t apart_ms(uint32_t one_ms, uint32_t other_ms) {
    const int32_t gap_ms = static_cast<int32_t>(one_ms - other_ms);
    return static_cast<uint32_t>(gap_ms < 0 ? -int64_t{gap_ms} : gap_ms);
}

int32_t distance_m(const CprPosition& from, const CprPosition& to) {
    const wire::NorthEast offset = wire::north_east_m(from.latitude_1e7, from.longitude_1e7,
                                                      to.latitude_1e7, to.longitude_1e7);
    return static_cast<int32_t>(hypotenuse(offset.north_m, offset.east_m));
}

bool agrees(const CprPosition& one, const CprPosition& other, CprFormat format) {
    const int64_t north_1e7 = int64_t{other.latitude_1e7} - one.latitude_1e7;
    const int64_t east_1e7 =
        wrapped_longitude_1e7(int64_t{other.longitude_1e7} - one.longitude_1e7);
    if (north_1e7 > kFarApart1e7 || north_1e7 < -kFarApart1e7 || east_1e7 > kFarApart1e7 ||
        east_1e7 < -kFarApart1e7)
        return false;
    const int64_t cosine_of_latitude = cosine(wire::latitude_angle16(one.latitude_1e7));
    const int64_t north_mm = north_1e7 * kMicrometresPerE7 / kMicrometresPerMillimetre;
    const int64_t east_mm =
        east_1e7 * kMicrometresPerE7 * cosine_of_latitude / (kMicrometresPerMillimetre * kTrigOne);
    const int64_t ceiling_mm =
        format == CprFormat::Surface ? kSurfaceAgreementCeilingMm : kAirborneAgreementCeilingMm;
    return north_mm * north_mm + east_mm * east_mm <= ceiling_mm * ceiling_mm;
}

uint32_t pair_max_age_ms(CprFormat format, bool slow) {
    if (format == CprFormat::Airborne) return kAirbornePairMaxAgeMs;
    return slow ? kSlowSurfacePairMaxAgeMs : kSurfacePairMaxAgeMs;
}

int32_t jump_ceiling_m(CprFormat before, CprFormat now) {
    if (before != now) return kTransitionJumpCeilingM;
    return now == CprFormat::Surface ? kSurfaceJumpCeilingM : kAirborneJumpCeilingM;
}

int32_t state_code(AirGround state) { return static_cast<int32_t>(state); }

}

bool Assembly::fold(const Message& message, uint32_t at_ms, const Receipt& received,
                    const Reference& reference, Track& out) {
    if (own_address_ != 0 && message.address == own_address_) return false;
    Aircraft* aircraft = admit(message, at_ms, reference);
    if (aircraft == nullptr) return false;

    expire(*aircraft, at_ms);
    aircraft->heard_at_ms = at_ms;
    if (!aircraft->position_valid) {
        aircraft->received = received;
        aircraft->rssi_dbm = message.reception.rssi_dbm;
        aircraft->rssi_valid = message.reception.rssi_valid;
    }
    const Reception& reception = message.reception;
    switch (message.kind) {
        case Kind::Identification:
            aircraft->identified = true;
            aircraft->emitter_type_code = message.type_code;
            aircraft->emitter_category = message.identification.category;
            break;
        case Kind::SurfacePosition: {
            const SurfacePosition& surface = message.surface_position;
            aircraft->air_ground = {state_code(AirGround::Ground), at_ms, true};
            fold_surface_motion(*aircraft, surface, at_ms);
            const bool slow =
                surface.speed_mkt.valid && surface.speed_mkt.value < kSlowSurfaceSpeedCeilingMkt;
            fold_position(*aircraft, CprFormat::Surface, surface.cpr, slow, at_ms, received,
                          reception, reference);
            break;
        }
        case Kind::AirbornePosition: {
            const AirbornePosition& airborne = message.airborne_position;
            aircraft->air_ground = {state_code(AirGround::Airborne), at_ms, true};
            if (airborne.pressure_altitude_valid)
                aircraft->pressure_altitude_ft = {airborne.pressure_altitude_ft, at_ms, true};
            if (airborne.gnss_height_valid)
                aircraft->gnss_height_ft = {airborne.gnss_height_ft, at_ms, true};
            fold_position(*aircraft, CprFormat::Airborne, airborne.cpr, false, at_ms, received,
                          reception, reference);
            break;
        }
        case Kind::Velocity:
            aircraft->air_ground = {state_code(AirGround::Airborne), at_ms, true};
            fold_velocity(*aircraft, message.velocity, at_ms);
            break;
        case Kind::OperationalStatus: {
            const OperationalStatus& status = message.operational_status;
            aircraft->version = status.version;
            aircraft->version_valid = true;
            if (status.gva_valid) {
                aircraft->gva = status.gva;
                aircraft->gva_valid = true;
            }
            break;
        }
    }
    const bool says_where = message.kind == Kind::SurfacePosition ||
                            message.kind == Kind::AirbornePosition ||
                            message.kind == Kind::Velocity;
    if (!says_where && message.capability == kGroundCapability)
        aircraft->air_ground = {state_code(AirGround::Ground), at_ms, true};
    if (!says_where && message.capability == kAirborneCapability)
        aircraft->air_ground = {state_code(AirGround::Airborne), at_ms, true};
    observe(*aircraft, out);
    return true;
}

void Assembly::age_out(uint32_t now_ms) {
    for (Aircraft& aircraft : aircraft_) {
        if (!aircraft.used) continue;
        if (since_ms(now_ms, aircraft.heard_at_ms) > kAdsbAircraftForgetMs)
            aircraft.used = false;
        else
            expire(aircraft, now_ms);
    }
}

int Assembly::count() const {
    int used = 0;
    for (const Aircraft& aircraft : aircraft_) used += aircraft.used ? 1 : 0;
    return used;
}

Assembly::Aircraft* Assembly::admit(const Message& message, uint32_t at_ms,
                                    const Reference& reference) {
    for (Aircraft& aircraft : aircraft_) {
        if (!aircraft.used || aircraft.address != message.address) continue;
        if (since_ms(at_ms, aircraft.heard_at_ms) > kAdsbAircraftForgetMs)
            arrive(aircraft, message.address, at_ms);
        return &aircraft;
    }
    for (Aircraft& aircraft : aircraft_) {
        if (aircraft.used) continue;
        arrive(aircraft, message.address, at_ms);
        return &aircraft;
    }

    const Weight incoming{false, range_of(message, reference), 0};
    const bool ranked = incoming.range_m != kUnknownRangeM;
    if (ranked && !(ranged_ && same_place(ranged_from_, reference.position))) {
        for (Aircraft& aircraft : aircraft_) aircraft.range_current = false;
        ranged_from_ = reference.position;
        ranged_ = true;
    }
    Aircraft* victim = nullptr;
    Weight least{};
    for (Aircraft& aircraft : aircraft_) {
        if (ranked && !aircraft.range_current) {
            aircraft.range_m = range_now(aircraft, reference);
            aircraft.range_current = true;
        }
        const Weight weight = weight_of(aircraft, at_ms, ranked, reference.valid);
        if (victim == nullptr || matters_less(weight, least)) {
            victim = &aircraft;
            least = weight;
        }
    }
    if (!matters_less(least, incoming)) {
        refusals_++;
        return nullptr;
    }
    if (!least.forgotten) evictions_++;
    arrive(*victim, message.address, at_ms);
    return victim;
}

void Assembly::fold_position(Aircraft& aircraft, CprFormat format, const Cpr& cpr, bool slow,
                             uint32_t at_ms, const Receipt& received, const Reception& reception,
                             const Reference& reference) {
    CprHalf& half = cpr.odd ? aircraft.odd : aircraft.even;
    half = CprHalf{cpr, at_ms, format, slow, true};
    aircraft.range_current = false;
    const CprHalf& other = cpr.odd ? aircraft.even : aircraft.odd;

    const auto accept = [&](const CprPosition& position) {
        aircraft.position = position;
        aircraft.position_at_ms = at_ms;
        aircraft.position_format = format;
        aircraft.position_valid = true;
        aircraft.received = received;
        aircraft.rssi_dbm = reception.rssi_dbm;
        aircraft.rssi_valid = reception.rssi_valid;
    };
    if (aircraft.position_valid && since_ms(at_ms, aircraft.position_at_ms) <= kTrackJumpWindowMs) {
        CprPosition local;
        if (!cpr_local(format, cpr, aircraft.position, local)) {
            implausible_++;
            return;
        }
        if (!aircraft.confirmed && !confirm(aircraft, half, local)) {
            implausible_++;
            aircraft.position_valid = false;
            return;
        }
        if (distance_m(aircraft.position, local) >
            jump_ceiling_m(aircraft.position_format, format)) {
            implausible_++;
            return;
        }
        accept(local);
        return;
    }

    if (!pairs(half, other)) return;
    if (format == CprFormat::Surface && !reference.valid) return;
    CprPosition decoded;
    if (!cpr_global(format, cpr, other.cpr, reference.position, decoded)) return;
    if (reference.valid && distance_m(reference.position, decoded) > kAdsbRangeCeilingM) {
        implausible_++;
        aircraft.even.held = false;
        aircraft.odd.held = false;
        return;
    }
    accept(decoded);
    aircraft.confirmed = false;
    aircraft.even.founding = true;
    aircraft.odd.founding = true;
}

bool Assembly::confirm(Aircraft& aircraft, const CprHalf& newest, const CprPosition& local) {
    const CprHalf& other = newest.cpr.odd ? aircraft.even : aircraft.odd;
    const bool independent = pairs(newest, other) && !other.founding;
    CprPosition global;
    if (!independent ||
        !cpr_global(newest.format, newest.cpr, other.cpr, aircraft.position, global))
        return true;
    aircraft.confirmed = agrees(global, local, newest.format);
    return aircraft.confirmed;
}

void Assembly::fold_velocity(Aircraft& aircraft, const Velocity& velocity, uint32_t at_ms) {
    const bool over_ground = velocity.subtype == 1 || velocity.subtype == 2;
    if (over_ground && velocity.east_kt.valid && velocity.north_kt.valid) {
        aircraft.motion = Motion{};
        aircraft.motion.source = MotionSource::Velocity;
        aircraft.motion.east_kt = static_cast<int16_t>(velocity.east_kt.value);
        aircraft.motion.north_kt = static_cast<int16_t>(velocity.north_kt.value);
        aircraft.motion_at_ms = at_ms;
    }
    if (velocity.climb_fpm.valid) {
        aircraft.climb_fpm = {velocity.climb_fpm.value, at_ms, true};
        aircraft.climb_source = velocity.climb_source;
    }
    if (velocity.gnss_minus_pressure_ft.valid && !velocity.gnss_minus_pressure_ft.saturated)
        aircraft.height_difference_ft = {velocity.gnss_minus_pressure_ft.value, at_ms, true};
}

void Assembly::fold_surface_motion(Aircraft& aircraft, const SurfacePosition& surface,
                                   uint32_t at_ms) {
    const bool stopped = surface.speed_mkt.valid && surface.speed_mkt.value == 0;
    if (!surface.speed_mkt.valid || !(surface.track_valid || stopped)) return;
    aircraft.motion = Motion{};
    aircraft.motion.source = MotionSource::Surface;
    aircraft.motion.speed_mkt = surface.speed_mkt.value;
    aircraft.motion.track_c7 = surface.track_c7;
    aircraft.motion.track_valid = surface.track_valid;
    aircraft.motion_at_ms = at_ms;
}

void Assembly::observe(const Aircraft& aircraft, Track& out) {
    out = Track{};
    out.address = aircraft.address;
    out.identified = aircraft.identified;
    out.emitter_type_code = aircraft.emitter_type_code;
    out.emitter_category = aircraft.emitter_category;
    out.received = aircraft.received;
    out.rssi_dbm = aircraft.rssi_dbm;
    out.rssi_valid = aircraft.rssi_valid;
    out.at_ms = aircraft.position_valid ? aircraft.position_at_ms : aircraft.heard_at_ms;
    out.position_valid = aircraft.position_valid;
    if (aircraft.position_valid) out.position = aircraft.position;
    const auto age_ms = [&out](uint32_t field_at_ms) { return elapsed_ms(out.at_ms, field_at_ms); };

    if (aircraft.air_ground.valid) {
        out.air_ground = static_cast<AirGround>(aircraft.air_ground.value);
        out.air_ground_age_ms = age_ms(aircraft.air_ground.at_ms);
    }
    if (aircraft.motion.source != MotionSource::None) {
        out.motion = aircraft.motion;
        out.motion_age_ms = age_ms(aircraft.motion_at_ms);
    }
    out.climb_valid = aircraft.climb_fpm.valid;
    if (aircraft.climb_fpm.valid) {
        out.climb_fpm = aircraft.climb_fpm.value;
        out.climb_source = aircraft.climb_source;
        out.climb_age_ms = age_ms(aircraft.climb_fpm.at_ms);
    }
    const Timed& pressure = aircraft.pressure_altitude_ft;
    out.pressure_altitude_valid = pressure.valid;
    if (pressure.valid) {
        out.pressure_altitude_ft = pressure.value;
        out.pressure_altitude_age_ms = age_ms(pressure.at_ms);
    }

    const Timed& difference = aircraft.height_difference_ft;
    out.height_difference_valid = difference.valid;
    if (difference.valid) {
        out.height_difference_ft = difference.value;
        out.height_difference_age_ms = age_ms(difference.at_ms);
    }
    const int32_t skew_ms = pressure.valid && difference.valid
                                ? elapsed_ms(pressure.at_ms, difference.at_ms)
                                : INT32_MAX;
    const bool derived = skew_ms >= -static_cast<int32_t>(kHeightDifferenceMaxAgeMs) &&
                         skew_ms <= static_cast<int32_t>(kHeightDifferenceMaxAgeMs);
    const uint32_t derived_at_ms = skew_ms >= 0 ? difference.at_ms : pressure.at_ms;
    const Timed& direct = aircraft.gnss_height_ft;
    const bool direct_newer =
        direct.valid && (!derived || elapsed_ms(direct.at_ms, derived_at_ms) >= 0);
    out.gnss_height_valid = direct_newer || derived;
    if (direct_newer) {
        out.gnss_height_ft = direct.value;
        out.gnss_height_age_ms = age_ms(direct.at_ms);
    } else if (derived) {
        out.gnss_height_ft = pressure.value + difference.value;
        out.gnss_height_age_ms = age_ms(derived_at_ms);
    }

    out.version = aircraft.version;
    out.version_valid = aircraft.version_valid;
    out.gva = aircraft.gva;
    out.gva_valid = aircraft.gva_valid;
}

bool Assembly::pairs(const CprHalf& newest, const CprHalf& other) {
    return other.held && other.format == newest.format &&
           apart_ms(newest.at_ms, other.at_ms) <=
               pair_max_age_ms(newest.format, newest.slow && other.slow);
}

void Assembly::arrive(Aircraft& aircraft, uint32_t address, uint32_t at_ms) {
    aircraft = Aircraft{};
    aircraft.address = address;
    aircraft.heard_at_ms = at_ms;
    aircraft.used = true;
}

void Assembly::expire(Aircraft& aircraft, uint32_t now_ms) {
    for (CprHalf* half : {&aircraft.even, &aircraft.odd}) {
        if (half->held &&
            since_ms(now_ms, half->at_ms) > pair_max_age_ms(half->format, half->slow)) {
            half->held = false;
            aircraft.range_current = false;
        }
    }
    if (aircraft.position_valid &&
        since_ms(now_ms, aircraft.position_at_ms) > kTrackPositionMaxAgeMs) {
        aircraft.position_valid = false;
        aircraft.confirmed = false;
        aircraft.range_current = false;
    }
    if (aircraft.height_difference_ft.valid &&
        since_ms(now_ms, aircraft.height_difference_ft.at_ms) > kHeightDifferenceMaxAgeMs)
        aircraft.height_difference_ft.valid = false;
    for (Timed* field : {&aircraft.pressure_altitude_ft, &aircraft.gnss_height_ft,
                         &aircraft.climb_fpm, &aircraft.air_ground})
        if (field->valid && since_ms(now_ms, field->at_ms) > kAdsbAircraftForgetMs)
            field->valid = false;
    if (aircraft.motion.source != MotionSource::None &&
        since_ms(now_ms, aircraft.motion_at_ms) > kAdsbAircraftForgetMs)
        aircraft.motion.source = MotionSource::None;
}

bool Assembly::matters_less(const Weight& weight, const Weight& other) {
    if (weight.forgotten != other.forgotten) return weight.forgotten;
    if (weight.range_m != other.range_m) return weight.range_m > other.range_m;
    return weight.silent_ms > other.silent_ms;
}

Assembly::Weight Assembly::weight_of(const Aircraft& aircraft, uint32_t at_ms, bool ranked,
                                     bool referenced) {
    const uint32_t silence_ms = since_ms(at_ms, aircraft.heard_at_ms);
    const bool placed = aircraft.position_valid || aircraft.even.held || aircraft.odd.held;
    int32_t range_m = kUnknownRangeM;
    if (ranked) range_m = aircraft.range_m;
    if (!ranked && referenced && placed) range_m = 0;
    return Weight{silence_ms > kAdsbAircraftForgetMs, range_m, silence_ms};
}

int32_t Assembly::range_now(const Aircraft& aircraft, const Reference& reference) {
    if (!reference.valid) return kUnknownRangeM;
    if (aircraft.position_valid) return distance_m(reference.position, aircraft.position);
    const bool odd_newest =
        aircraft.odd.held &&
        (!aircraft.even.held || elapsed_ms(aircraft.odd.at_ms, aircraft.even.at_ms) > 0);
    const CprHalf& newest = odd_newest ? aircraft.odd : aircraft.even;
    if (!newest.held) return kUnknownRangeM;
    return tentative_range_m(newest.format, newest.cpr, reference);
}

int32_t Assembly::range_of(const Message& message, const Reference& reference) {
    if (!reference.valid) return kUnknownRangeM;
    if (message.kind == Kind::SurfacePosition)
        return tentative_range_m(CprFormat::Surface, message.surface_position.cpr, reference);
    if (message.kind == Kind::AirbornePosition)
        return tentative_range_m(CprFormat::Airborne, message.airborne_position.cpr, reference);
    return kUnknownRangeM;
}

int32_t Assembly::tentative_range_m(CprFormat format, const Cpr& cpr, const Reference& reference) {
    CprPosition tentative;
    if (!cpr_local(format, cpr, reference.position, tentative)) return kUnknownRangeM;
    return distance_m(reference.position, tentative);
}

}
