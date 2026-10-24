#ifndef SKYBLIP_CORE_UNITS_UNITS_H
#define SKYBLIP_CORE_UNITS_UNITS_H

#include <cstdint>

#include "core/util/intmath.h"

namespace skyblip {

struct Millimetres {
    int32_t value{0};
    constexpr Millimetres() = default;
    constexpr explicit Millimetres(int32_t millimetres) : value(millimetres) {}
    constexpr bool operator==(Millimetres other) const { return value == other.value; }
};

struct Metres {
    int32_t value{0};
    constexpr Metres() = default;
    constexpr explicit Metres(int32_t metres) : value(metres) {}
    constexpr bool operator==(Metres other) const { return value == other.value; }
};

struct Feet {
    int32_t value{0};
    constexpr Feet() = default;
    constexpr explicit Feet(int32_t feet) : value(feet) {}
};

constexpr int32_t kMillimetresPerMetre = 1000;
constexpr int32_t kMillisecondsPerSecond = 1000;

constexpr Millimetres to_millimetres(Metres metres) {
    return Millimetres(metres.value * kMillimetresPerMetre);
}
constexpr Metres to_metres(Millimetres millimetres) {
    return Metres(divide_rounded(millimetres.value, kMillimetresPerMetre));
}
constexpr Metres to_metres(Feet feet) { return Metres((feet.value * 2497 + 4096) >> 13); }
constexpr Feet to_feet(Metres metres) { return Feet((metres.value * 3360 + 512) >> 10); }
constexpr Feet to_feet(Millimetres millimetres) {
    return Feet(static_cast<int32_t>(
        divide_rounded<int64_t>(static_cast<int64_t>(millimetres.value) * 3360, 1024000)));
}

constexpr int32_t kMetresPerNm = 1852;

struct NauticalMilesE1 {
    int32_t value{0};
    constexpr NauticalMilesE1() = default;
    constexpr explicit NauticalMilesE1(int32_t tenths) : value(tenths) {}
};

constexpr NauticalMilesE1 to_nm_e1(Metres metres) {
    return NauticalMilesE1(divide_rounded(metres.value * 10, kMetresPerNm));
}

struct QuarterMetresPerSecond {
    uint16_t value{0};
    constexpr QuarterMetresPerSecond() = default;
    constexpr explicit QuarterMetresPerSecond(uint16_t quarters) : value(quarters) {}
};
struct EighthMetresPerSecond {
    int16_t value{0};
    constexpr EighthMetresPerSecond() = default;
    constexpr explicit EighthMetresPerSecond(int16_t eighths) : value(eighths) {}
};
struct MetresPerSecond {
    int32_t value{0};
    constexpr MetresPerSecond() = default;
    constexpr explicit MetresPerSecond(int32_t metres_per_second) : value(metres_per_second) {}
};
struct MillimetresPerSecond {
    int32_t value{0};
    constexpr MillimetresPerSecond() = default;
    constexpr explicit MillimetresPerSecond(int32_t millimetres_per_second)
        : value(millimetres_per_second) {}
};
struct Knots {
    int32_t value{0};
    constexpr Knots() = default;
    constexpr explicit Knots(int32_t knots) : value(knots) {}
};
struct KilometresPerHour {
    int32_t value{0};
    constexpr KilometresPerHour() = default;
    constexpr explicit KilometresPerHour(int32_t kilometres_per_hour)
        : value(kilometres_per_hour) {}
};
struct FeetPerMinute {
    int32_t value{0};
    constexpr FeetPerMinute() = default;
    constexpr explicit FeetPerMinute(int32_t feet_per_minute) : value(feet_per_minute) {}
};

constexpr int32_t kMillimetresPerSpeedQ = 250;
constexpr int32_t kMillimetresPerClimbE8 = 125;

constexpr MillimetresPerSecond to_mm_s(QuarterMetresPerSecond speed) {
    return MillimetresPerSecond(static_cast<int32_t>(speed.value) * kMillimetresPerSpeedQ);
}
constexpr MillimetresPerSecond to_mm_s(EighthMetresPerSecond climb) {
    return MillimetresPerSecond(static_cast<int32_t>(climb.value) * kMillimetresPerClimbE8);
}
constexpr QuarterMetresPerSecond to_speed_q(MillimetresPerSecond speed) {
    const int32_t quarters =
        divide_rounded(speed.value < 0 ? 0 : speed.value, kMillimetresPerSpeedQ);
    return QuarterMetresPerSecond(static_cast<uint16_t>(quarters > 0xFFFF ? 0xFFFF : quarters));
}
constexpr EighthMetresPerSecond to_climb_e8(MillimetresPerSecond climb) {
    const int32_t eighths = divide_rounded(climb.value, kMillimetresPerClimbE8);
    return EighthMetresPerSecond(
        static_cast<int16_t>(eighths < -32768 ? -32768 : (eighths > 32767 ? 32767 : eighths)));
}
constexpr MetresPerSecond to_mps(MillimetresPerSecond speed) {
    return MetresPerSecond(divide_rounded(speed.value, kMillimetresPerMetre));
}
constexpr MetresPerSecond to_mps(QuarterMetresPerSecond speed) { return to_mps(to_mm_s(speed)); }

// INFO: fc 18sep26 1 m/s = 1.94384 kt = 3.6 km/h, rounded: truncated, 60 kt flown reads 59
constexpr Knots to_knots(MillimetresPerSecond speed) {
    return Knots(static_cast<int32_t>(
        divide_rounded<int64_t>(static_cast<int64_t>(speed.value) * 194384, 100000000)));
}
constexpr Knots to_knots(QuarterMetresPerSecond speed) { return to_knots(to_mm_s(speed)); }
constexpr KilometresPerHour to_kmh(MillimetresPerSecond speed) {
    return KilometresPerHour(static_cast<int32_t>(
        divide_rounded<int64_t>(static_cast<int64_t>(speed.value) * 36, 10000)));
}
constexpr KilometresPerHour to_kmh(QuarterMetresPerSecond speed) { return to_kmh(to_mm_s(speed)); }
constexpr FeetPerMinute to_feet_per_minute(MillimetresPerSecond speed) {
    return FeetPerMinute(static_cast<int32_t>(
        divide_rounded<int64_t>(static_cast<int64_t>(speed.value) * 19685, 100000)));
}

struct CentiDegrees {
    int32_t value{0};
    constexpr CentiDegrees() = default;
    constexpr explicit CentiDegrees(int32_t centi_degrees) : value(centi_degrees) {}
};
struct Cordic9 {
    uint16_t value{0};
    constexpr Cordic9() = default;
    constexpr explicit Cordic9(uint16_t cordic) : value(static_cast<uint16_t>(cordic & 0x1FF)) {}
};
struct Degrees {
    uint16_t value{0};
    constexpr Degrees() = default;
    constexpr explicit Degrees(uint16_t degrees) : value(degrees) {}
};

constexpr int32_t kCentiDegreesPerTurn = 36000;
constexpr int32_t kCordic9PerTurn = 512;

constexpr CentiDegrees wrapped(CentiDegrees angle) {
    return CentiDegrees(((angle.value % kCentiDegreesPerTurn) + kCentiDegreesPerTurn) %
                        kCentiDegreesPerTurn);
}
constexpr Cordic9 to_cordic9(CentiDegrees angle) {
    return Cordic9(static_cast<uint16_t>(
        divide_rounded(wrapped(angle).value * kCordic9PerTurn, kCentiDegreesPerTurn) %
        kCordic9PerTurn));
}
constexpr CentiDegrees to_centi_degrees(Cordic9 cordic) {
    return CentiDegrees(
        divide_rounded(static_cast<int32_t>(cordic.value) * kCentiDegreesPerTurn, kCordic9PerTurn));
}
constexpr Degrees to_degrees(CentiDegrees angle) {
    return Degrees(static_cast<uint16_t>(divide_rounded(wrapped(angle).value, 100) % 360));
}
constexpr Degrees to_degrees(Cordic9 cordic) { return to_degrees(to_centi_degrees(cordic)); }

constexpr int32_t kAngle16PerTurn = 65536;

// INFO: fc 19sep26 the angle core/util/intmath's sine table is indexed by, 65536 to the turn
constexpr int16_t to_angle16(CentiDegrees angle) {
    return static_cast<int16_t>(static_cast<uint16_t>(divide_rounded<int64_t>(
        static_cast<int64_t>(wrapped(angle).value) * kAngle16PerTurn, kCentiDegreesPerTurn)));
}
constexpr int16_t to_angle16(Cordic9 cordic) { return to_angle16(to_centi_degrees(cordic)); }
constexpr CentiDegrees centi_degrees_of_angle16(uint16_t angle16) {
    return CentiDegrees(static_cast<int32_t>(
        divide_rounded<int64_t>(static_cast<int64_t>(angle16) * kCentiDegreesPerTurn,
                                kAngle16PerTurn) %
        kCentiDegreesPerTurn));
}

}  // namespace skyblip

#endif
