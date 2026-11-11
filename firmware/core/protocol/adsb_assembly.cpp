#include "core/protocol/adsb_assembly.h"

#include <cstdint>
#include <initializer_list>

#include "core/flight/extrapolate.h"
#include "core/flight/state.h"
#include "core/model/ownship.h"
#include "core/protocol/adsb_category.h"
#include "core/units/units.h"
#include "core/util/intmath.h"

namespace skyblip::protocol {

namespace {

constexpr uint8_t kGroundCapability = 4;
constexpr uint8_t kAirborneCapability = 5;
constexpr int32_t kUnknownRangeM = INT32_MAX;
constexpr uint8_t kCordic9PerCordic7 = 4;
constexpr int64_t kSecondsPerHour = 3600;
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
    model::Ownship own{};
    own.fix_valid = true;
    own.latitude_1e7 = from.latitude_1e7;
    own.longitude_1e7 = from.longitude_1e7;
    model::AircraftObservation target{};
    target.position_valid = true;
    target.latitude_1e7 = to.latitude_1e7;
    target.longitude_1e7 = to.longitude_1e7;
    int32_t north_m = 0, east_m = 0, up_m = 0;
    bool up_valid = false;
    flight::relative_north_east_up(own, target, north_m, east_m, up_m, up_valid);
    return static_cast<int32_t>(hypotenuse(north_m, east_m));
}

bool agrees(const CprPosition& one, const CprPosition& other, CprFormat format) {
    const int64_t north_1e7 = int64_t{other.latitude_1e7} - one.latitude_1e7;
    const int64_t east_1e7 =
        wrapped_longitude_1e7(int64_t{other.longitude_1e7} - one.longitude_1e7);
    if (north_1e7 > kFarApart1e7 || north_1e7 < -kFarApart1e7 || east_1e7 > kFarApart1e7 ||
        east_1e7 < -kFarApart1e7)
        return false;
    const int64_t cosine_of_latitude =
        cosine(static_cast<int16_t>(int64_t{one.latitude_1e7} * kAngle16PerTurn / kE7PerTurn));
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

uint8_t state_code(flight::FlightState state) { return static_cast<uint8_t>(state); }

}

bool AdsbAssembly::fold(const AdsbMessage& message, uint32_t at_ms, const events::Stamp& received,
                        const AdsbReference& reference, model::AircraftObservation& out) {
    if (own_address_ != 0 && message.address == own_address_) return false;
    Aircraft* aircraft = admit(message, at_ms, reference);
    if (aircraft == nullptr) return false;

    expire(*aircraft, at_ms);
    aircraft->heard_at_ms = at_ms;
    if (!aircraft->position_valid) {
        aircraft->received = received;
        aircraft->rssi_dbm = message.reception.rssi_dbm;
    }
    const int8_t rssi_dbm = message.reception.rssi_dbm;
    switch (message.kind) {
        case AdsbKind::Identification:
            aircraft->aircraft_category =
                adsb_emitter_to_adsl_category(message.type_code, message.identification.category);
            break;
        case AdsbKind::SurfacePosition: {
            const AdsbSurfacePosition& surface = message.surface_position;
            aircraft->flight_state = {state_code(flight::FlightState::Ground), at_ms, true};
            fold_surface_motion(*aircraft, surface, at_ms);
            const bool slow =
                surface.speed_mkt.valid && surface.speed_mkt.value < kSlowSurfaceSpeedCeilingMkt;
            fold_position(*aircraft, CprFormat::Surface, surface.cpr, slow, at_ms, received,
                          rssi_dbm, reference);
            break;
        }
        case AdsbKind::AirbornePosition: {
            const AdsbAirbornePosition& airborne = message.airborne_position;
            aircraft->flight_state = {state_code(flight::FlightState::Airborne), at_ms, true};
            if (airborne.pressure_altitude_valid)
                aircraft->pressure_altitude_ft = {airborne.pressure_altitude_ft, at_ms, true};
            if (airborne.gnss_height_valid)
                aircraft->gnss_height_ft = {airborne.gnss_height_ft, at_ms, true};
            fold_position(*aircraft, CprFormat::Airborne, airborne.cpr, false, at_ms, received,
                          rssi_dbm, reference);
            break;
        }
        case AdsbKind::Velocity:
            aircraft->flight_state = {state_code(flight::FlightState::Airborne), at_ms, true};
            fold_velocity(*aircraft, message.velocity, at_ms);
            break;
        case AdsbKind::OperationalStatus: {
            const AdsbOperationalStatus& status = message.operational_status;
            aircraft->adsb_version = status.version;
            aircraft->adsb_version_valid = true;
            if (status.gva_valid) {
                aircraft->gva = status.gva;
                aircraft->gva_valid = true;
            }
            break;
        }
    }
    const bool says_where = message.kind == AdsbKind::SurfacePosition ||
                            message.kind == AdsbKind::AirbornePosition ||
                            message.kind == AdsbKind::Velocity;
    if (!says_where && message.capability == kGroundCapability)
        aircraft->flight_state = {state_code(flight::FlightState::Ground), at_ms, true};
    if (!says_where && message.capability == kAirborneCapability)
        aircraft->flight_state = {state_code(flight::FlightState::Airborne), at_ms, true};
    observe(*aircraft, out);
    return true;
}

void AdsbAssembly::age_out(uint32_t now_ms) {
    for (Aircraft& aircraft : aircraft_) {
        if (!aircraft.used) continue;
        if (since_ms(now_ms, aircraft.heard_at_ms) > kAdsbAircraftForgetMs)
            aircraft.used = false;
        else
            expire(aircraft, now_ms);
    }
}

int AdsbAssembly::count() const {
    int used = 0;
    for (const Aircraft& aircraft : aircraft_) used += aircraft.used ? 1 : 0;
    return used;
}

AdsbAssembly::Aircraft* AdsbAssembly::admit(const AdsbMessage& message, uint32_t at_ms,
                                            const AdsbReference& reference) {
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

void AdsbAssembly::fold_position(Aircraft& aircraft, CprFormat format, const AdsbCpr& cpr,
                                 bool slow, uint32_t at_ms, const events::Stamp& received,
                                 int8_t rssi_dbm, const AdsbReference& reference) {
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
        aircraft.rssi_dbm = rssi_dbm;
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

bool AdsbAssembly::confirm(Aircraft& aircraft, const CprHalf& newest, const CprPosition& local) {
    const CprHalf& other = newest.cpr.odd ? aircraft.even : aircraft.odd;
    const bool independent = pairs(newest, other) && !other.founding;
    CprPosition global;
    if (!independent ||
        !cpr_global(newest.format, newest.cpr, other.cpr, aircraft.position, global))
        return true;
    aircraft.confirmed = agrees(global, local, newest.format);
    return aircraft.confirmed;
}

void AdsbAssembly::fold_velocity(Aircraft& aircraft, const AdsbVelocity& velocity, uint32_t at_ms) {
    const bool over_ground = velocity.subtype == 1 || velocity.subtype == 2;
    if (over_ground && velocity.east_kt.valid && velocity.north_kt.valid) {
        const int32_t east_mm_s = to_mm_s(Knots(velocity.east_kt.value)).value;
        const int32_t north_mm_s = to_mm_s(Knots(velocity.north_kt.value)).value;
        const uint16_t track_angle16 = static_cast<uint16_t>(arctangent(east_mm_s, north_mm_s));
        aircraft.speed_q = to_speed_q(MillimetresPerSecond(
                                          static_cast<int32_t>(hypotenuse(east_mm_s, north_mm_s))))
                               .value;
        aircraft.track_c9 = to_cordic9(centi_degrees_of_angle16(track_angle16)).value;
        aircraft.speed_at_ms = at_ms;
        aircraft.speed_valid = true;
    }
    if (velocity.climb_fpm.valid) {
        aircraft.climb_e8 = {to_climb_e8(to_mm_s(FeetPerMinute(velocity.climb_fpm.value))).value,
                             at_ms, true};
        aircraft.climb_source = velocity.climb_source == AdsbHeightSource::Gnss
                                    ? model::ClimbSource::Gnss
                                    : model::ClimbSource::Barometric;
    }
    if (velocity.gnss_minus_pressure_ft.valid && !velocity.gnss_minus_pressure_ft.saturated)
        aircraft.height_difference_ft = {velocity.gnss_minus_pressure_ft.value, at_ms, true};
}

void AdsbAssembly::fold_surface_motion(Aircraft& aircraft, const AdsbSurfacePosition& surface,
                                       uint32_t at_ms) {
    const bool stopped = surface.speed_mkt.valid && surface.speed_mkt.value == 0;
    if (!surface.speed_mkt.valid || !(surface.track_valid || stopped)) return;
    const int32_t speed_mm_s = static_cast<int32_t>(
        divide_rounded<int64_t>(int64_t{surface.speed_mkt.value} * kMetresPerNm, kSecondsPerHour));
    aircraft.speed_q = to_speed_q(MillimetresPerSecond(speed_mm_s)).value;
    aircraft.track_c9 =
        surface.track_valid ? static_cast<uint16_t>(surface.track_c7 * kCordic9PerCordic7) : 0;
    aircraft.speed_at_ms = at_ms;
    aircraft.speed_valid = true;
}

void AdsbAssembly::observe(const Aircraft& aircraft, model::AircraftObservation& out) {
    out = model::AircraftObservation{};
    out.address = aircraft.address;
    out.address_mapping_table = kIcaoAddressMappingTable;
    out.source = model::Source::Adsb;
    out.aircraft_category = aircraft.aircraft_category;
    out.received = aircraft.received;
    out.rssi_dbm = aircraft.rssi_dbm;
    out.at_ms = aircraft.position_valid ? aircraft.position_at_ms : aircraft.heard_at_ms;
    out.position_valid = aircraft.position_valid;
    if (aircraft.position_valid) {
        out.latitude_1e7 = aircraft.position.latitude_1e7;
        out.longitude_1e7 = aircraft.position.longitude_1e7;
    }
    const auto age_ms = [&out](uint32_t field_at_ms) { return elapsed_ms(out.at_ms, field_at_ms); };

    if (aircraft.flight_state.valid) {
        out.flight_state = static_cast<uint8_t>(aircraft.flight_state.value);
        out.flight_state_age_ms = age_ms(aircraft.flight_state.at_ms);
    }
    out.speed_valid = aircraft.speed_valid;
    if (aircraft.speed_valid) {
        out.speed_q = aircraft.speed_q;
        out.track_c9 = aircraft.track_c9;
        out.speed_age_ms = age_ms(aircraft.speed_at_ms);
    }
    out.climb_valid = aircraft.climb_e8.valid;
    if (aircraft.climb_e8.valid) {
        out.climb_e8 = static_cast<int16_t>(aircraft.climb_e8.value);
        out.climb_source = aircraft.climb_source;
        out.climb_age_ms = age_ms(aircraft.climb_e8.at_ms);
    }
    const Timed& pressure = aircraft.pressure_altitude_ft;
    out.pressure_altitude_valid = pressure.valid;
    if (pressure.valid) {
        out.pressure_altitude_ft = pressure.value;
        out.pressure_altitude_age_ms = age_ms(pressure.at_ms);
    }

    const Timed& difference = aircraft.height_difference_ft;
    const int32_t skew_ms = pressure.valid && difference.valid
                                ? elapsed_ms(pressure.at_ms, difference.at_ms)
                                : INT32_MAX;
    const bool derived = skew_ms >= -static_cast<int32_t>(kHeightDifferenceMaxAgeMs) &&
                         skew_ms <= static_cast<int32_t>(kHeightDifferenceMaxAgeMs);
    const uint32_t derived_at_ms = skew_ms >= 0 ? difference.at_ms : pressure.at_ms;
    const Timed& direct = aircraft.gnss_height_ft;
    const bool direct_newer =
        direct.valid && (!derived || elapsed_ms(direct.at_ms, derived_at_ms) >= 0);
    out.altitude_valid = direct_newer || derived;
    if (direct_newer) {
        out.altitude_m = to_metres(Feet(direct.value)).value;
        out.altitude_age_ms = age_ms(direct.at_ms);
    } else if (derived) {
        out.altitude_m = to_metres(Feet(pressure.value + difference.value)).value;
        out.altitude_age_ms = age_ms(derived_at_ms);
    }

    out.adsb_version = aircraft.adsb_version;
    out.adsb_version_valid = aircraft.adsb_version_valid;
    out.gva = aircraft.gva;
    out.gva_valid = aircraft.gva_valid;
}

bool AdsbAssembly::pairs(const CprHalf& newest, const CprHalf& other) {
    return other.held && other.format == newest.format &&
           apart_ms(newest.at_ms, other.at_ms) <=
               pair_max_age_ms(newest.format, newest.slow && other.slow);
}

void AdsbAssembly::arrive(Aircraft& aircraft, uint32_t address, uint32_t at_ms) {
    aircraft = Aircraft{};
    aircraft.address = address;
    aircraft.heard_at_ms = at_ms;
    aircraft.used = true;
}

void AdsbAssembly::expire(Aircraft& aircraft, uint32_t now_ms) {
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
                         &aircraft.climb_e8, &aircraft.flight_state})
        if (field->valid && since_ms(now_ms, field->at_ms) > kAdsbAircraftForgetMs)
            field->valid = false;
    if (aircraft.speed_valid && since_ms(now_ms, aircraft.speed_at_ms) > kAdsbAircraftForgetMs)
        aircraft.speed_valid = false;
}

bool AdsbAssembly::matters_less(const Weight& weight, const Weight& other) {
    if (weight.forgotten != other.forgotten) return weight.forgotten;
    if (weight.range_m != other.range_m) return weight.range_m > other.range_m;
    return weight.silent_ms > other.silent_ms;
}

AdsbAssembly::Weight AdsbAssembly::weight_of(const Aircraft& aircraft, uint32_t at_ms, bool ranked,
                                             bool referenced) {
    const uint32_t silence_ms = since_ms(at_ms, aircraft.heard_at_ms);
    const bool placed = aircraft.position_valid || aircraft.even.held || aircraft.odd.held;
    int32_t range_m = kUnknownRangeM;
    if (ranked) range_m = aircraft.range_m;
    if (!ranked && referenced && placed) range_m = 0;
    return Weight{silence_ms > kAdsbAircraftForgetMs, range_m, silence_ms};
}

int32_t AdsbAssembly::range_now(const Aircraft& aircraft, const AdsbReference& reference) {
    if (!reference.valid) return kUnknownRangeM;
    if (aircraft.position_valid) return distance_m(reference.position, aircraft.position);
    const bool odd_newest =
        aircraft.odd.held &&
        (!aircraft.even.held || elapsed_ms(aircraft.odd.at_ms, aircraft.even.at_ms) > 0);
    const CprHalf& newest = odd_newest ? aircraft.odd : aircraft.even;
    if (!newest.held) return kUnknownRangeM;
    return tentative_range_m(newest.format, newest.cpr, reference);
}

int32_t AdsbAssembly::range_of(const AdsbMessage& message, const AdsbReference& reference) {
    if (!reference.valid) return kUnknownRangeM;
    if (message.kind == AdsbKind::SurfacePosition)
        return tentative_range_m(CprFormat::Surface, message.surface_position.cpr, reference);
    if (message.kind == AdsbKind::AirbornePosition)
        return tentative_range_m(CprFormat::Airborne, message.airborne_position.cpr, reference);
    return kUnknownRangeM;
}

int32_t AdsbAssembly::tentative_range_m(CprFormat format, const AdsbCpr& cpr,
                                        const AdsbReference& reference) {
    CprPosition tentative;
    if (!cpr_local(format, cpr, reference.position, tentative)) return kUnknownRangeM;
    return distance_m(reference.position, tentative);
}

}
