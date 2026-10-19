#ifndef SKYBLIP_CORE_UNITS_UNITS_H
#define SKYBLIP_CORE_UNITS_UNITS_H

#include <cstdint>

#include "core/util/intmath.h"

namespace skyblip {

struct Millimetres {
    int32_t v{0};
    constexpr Millimetres() = default;
    constexpr explicit Millimetres(int32_t mm) : v(mm) {}
    constexpr bool operator==(Millimetres o) const { return v == o.v; }
};

struct Metres {
    int32_t v{0};
    constexpr Metres() = default;
    constexpr explicit Metres(int32_t m) : v(m) {}
    constexpr bool operator==(Metres o) const { return v == o.v; }
};

struct Feet {
    int32_t v{0};
    constexpr Feet() = default;
    constexpr explicit Feet(int32_t f) : v(f) {}
};

constexpr int32_t kMillimetresPerMetre = 1000;
constexpr int32_t kMillisecondsPerSecond = 1000;

constexpr Millimetres to_millimetres(Metres m) { return Millimetres(m.v * kMillimetresPerMetre); }
constexpr Metres to_metres(Millimetres mm) { return Metres(div_round(mm.v, kMillimetresPerMetre)); }
constexpr Metres to_metres(Feet f) { return Metres((f.v * 2497 + 4096) >> 13); }
constexpr Feet to_feet(Metres m) { return Feet((m.v * 3360 + 512) >> 10); }
constexpr Feet to_feet(Millimetres mm) {
    return Feet(
        static_cast<int32_t>(div_round<int64_t>(static_cast<int64_t>(mm.v) * 3360, 1024000)));
}

constexpr int32_t kMetresPerNm = 1852;

struct NauticalMilesE1 {
    int32_t v{0};
    constexpr NauticalMilesE1() = default;
    constexpr explicit NauticalMilesE1(int32_t tenths) : v(tenths) {}
};

constexpr NauticalMilesE1 to_nm_e1(Metres m) {
    return NauticalMilesE1(div_round(m.v * 10, kMetresPerNm));
}

struct QuarterMetresPerSecond {
    uint16_t v{0};
    constexpr QuarterMetresPerSecond() = default;
    constexpr explicit QuarterMetresPerSecond(uint16_t q) : v(q) {}
};
struct EighthMetresPerSecond {
    int16_t v{0};
    constexpr EighthMetresPerSecond() = default;
    constexpr explicit EighthMetresPerSecond(int16_t e) : v(e) {}
};
struct MetresPerSecond {
    int32_t v{0};
    constexpr MetresPerSecond() = default;
    constexpr explicit MetresPerSecond(int32_t m) : v(m) {}
};
struct MillimetresPerSecond {
    int32_t v{0};
    constexpr MillimetresPerSecond() = default;
    constexpr explicit MillimetresPerSecond(int32_t mm) : v(mm) {}
};
struct Knots {
    int32_t v{0};
    constexpr Knots() = default;
    constexpr explicit Knots(int32_t kt) : v(kt) {}
};
struct KilometresPerHour {
    int32_t v{0};
    constexpr KilometresPerHour() = default;
    constexpr explicit KilometresPerHour(int32_t kmh) : v(kmh) {}
};
struct FeetPerMinute {
    int32_t v{0};
    constexpr FeetPerMinute() = default;
    constexpr explicit FeetPerMinute(int32_t f) : v(f) {}
};

constexpr int32_t kMillimetresPerSpeedQ = 250;
constexpr int32_t kMillimetresPerClimbE8 = 125;

constexpr MillimetresPerSecond to_mm_s(QuarterMetresPerSecond q) {
    return MillimetresPerSecond(static_cast<int32_t>(q.v) * kMillimetresPerSpeedQ);
}
constexpr MillimetresPerSecond to_mm_s(EighthMetresPerSecond e) {
    return MillimetresPerSecond(static_cast<int32_t>(e.v) * kMillimetresPerClimbE8);
}
constexpr QuarterMetresPerSecond to_speed_q(MillimetresPerSecond mm) {
    const int32_t q = div_round(mm.v < 0 ? 0 : mm.v, kMillimetresPerSpeedQ);
    return QuarterMetresPerSecond(static_cast<uint16_t>(q > 0xFFFF ? 0xFFFF : q));
}
constexpr EighthMetresPerSecond to_climb_e8(MillimetresPerSecond mm) {
    const int32_t e = div_round(mm.v, kMillimetresPerClimbE8);
    return EighthMetresPerSecond(
        static_cast<int16_t>(e < -32768 ? -32768 : (e > 32767 ? 32767 : e)));
}
constexpr MetresPerSecond to_mps(MillimetresPerSecond mm) {
    return MetresPerSecond(div_round(mm.v, kMillimetresPerMetre));
}
constexpr MetresPerSecond to_mps(QuarterMetresPerSecond q) { return to_mps(to_mm_s(q)); }

// INFO: fc 18sep26 1 m/s = 1.94384 kt = 3.6 km/h, rounded: truncated, 60 kt flown reads 59
constexpr Knots to_knots(MillimetresPerSecond mm) {
    return Knots(
        static_cast<int32_t>(div_round<int64_t>(static_cast<int64_t>(mm.v) * 194384, 100000000)));
}
constexpr Knots to_knots(QuarterMetresPerSecond q) { return to_knots(to_mm_s(q)); }
constexpr KilometresPerHour to_kmh(MillimetresPerSecond mm) {
    return KilometresPerHour(
        static_cast<int32_t>(div_round<int64_t>(static_cast<int64_t>(mm.v) * 36, 10000)));
}
constexpr KilometresPerHour to_kmh(QuarterMetresPerSecond q) { return to_kmh(to_mm_s(q)); }
constexpr FeetPerMinute to_feet_per_minute(MillimetresPerSecond mm) {
    return FeetPerMinute(
        static_cast<int32_t>(div_round<int64_t>(static_cast<int64_t>(mm.v) * 19685, 100000)));
}

struct CentiDegrees {
    int32_t v{0};
    constexpr CentiDegrees() = default;
    constexpr explicit CentiDegrees(int32_t cd) : v(cd) {}
};
struct Cordic9 {
    uint16_t v{0};
    constexpr Cordic9() = default;
    constexpr explicit Cordic9(uint16_t c) : v(static_cast<uint16_t>(c & 0x1FF)) {}
};
struct Degrees {
    uint16_t v{0};
    constexpr Degrees() = default;
    constexpr explicit Degrees(uint16_t d) : v(d) {}
};

constexpr int32_t kCentiDegreesPerTurn = 36000;
constexpr int32_t kCordic9PerTurn = 512;

constexpr CentiDegrees wrapped(CentiDegrees cd) {
    return CentiDegrees(((cd.v % kCentiDegreesPerTurn) + kCentiDegreesPerTurn) %
                        kCentiDegreesPerTurn);
}
constexpr Cordic9 to_cordic9(CentiDegrees cd) {
    return Cordic9(static_cast<uint16_t>(
        div_round(wrapped(cd).v * kCordic9PerTurn, kCentiDegreesPerTurn) % kCordic9PerTurn));
}
constexpr CentiDegrees to_centi_degrees(Cordic9 c) {
    return CentiDegrees(
        div_round(static_cast<int32_t>(c.v) * kCentiDegreesPerTurn, kCordic9PerTurn));
}
constexpr Degrees to_degrees(CentiDegrees cd) {
    return Degrees(static_cast<uint16_t>(div_round(wrapped(cd).v, 100) % 360));
}
constexpr Degrees to_degrees(Cordic9 c) { return to_degrees(to_centi_degrees(c)); }

constexpr int32_t kAngle16PerTurn = 65536;

// INFO: fc 19sep26 the angle core/util/intmath's sine table is indexed by, 65536 to the turn
constexpr int16_t to_angle16(CentiDegrees cd) {
    return static_cast<int16_t>(static_cast<uint16_t>(div_round<int64_t>(
        static_cast<int64_t>(wrapped(cd).v) * kAngle16PerTurn, kCentiDegreesPerTurn)));
}
constexpr int16_t to_angle16(Cordic9 c) { return to_angle16(to_centi_degrees(c)); }
constexpr CentiDegrees centi_degrees_of_angle16(uint16_t angle16) {
    return CentiDegrees(static_cast<int32_t>(
        div_round<int64_t>(static_cast<int64_t>(angle16) * kCentiDegreesPerTurn, kAngle16PerTurn) %
        kCentiDegreesPerTurn));
}

}  // namespace skyblip

#endif
