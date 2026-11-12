#include <cmath>

#include "simulator/world/adsb_out.h"
#include "simulator/world/world.h"

namespace skyblip::simulator {

namespace {

// INFO: fc 13nov26 The 1090MHz Riddle Table 1.4: position and velocity 2 Hz, identification 0.2 Hz
constexpr uint64_t kSquitterPeriodUs = 500000;
constexpr uint64_t kIdentificationPeriodUs = 5000000;
// INFO: fc 13nov26 the same table, still on the ground: position 0.2 Hz, identification 0.1 Hz
constexpr uint64_t kStillPositionPeriodUs = 5000000;
constexpr uint64_t kStillIdentificationPeriodUs = 10000000;
// INFO: fc 13nov26 the world models no 1090 MHz path loss, so every squitter lands at -70 dBm
constexpr int8_t kSquitterLevelDbm = -70;
// INFO: fc 13nov26 the capability core/protocol/adsb_assembly.cpp reads: 4 on ground, 5 airborne
constexpr uint8_t kGroundCapability = 4;
constexpr uint8_t kAirborneCapability = 5;
constexpr double kFeetPerMetre = 1 / 0.3048;
constexpr double kKnotsPerMps = 1 / 0.514444;
constexpr double kFeetPerMinutePerMps = 60 * kFeetPerMetre;
constexpr double kPi = 3.14159265358979;

struct Due {
    uint64_t first{0};
    uint64_t end{0};
};

// INFO: fc 13nov26 every instant phase + k * period in (from, to], so a long step loses none
Due due(uint64_t from_us, uint64_t to_us, uint64_t period_us, uint64_t phase_us) {
    const auto sent_by = [&](uint64_t at_us) -> uint64_t {
        return at_us < phase_us ? 0 : (at_us - phase_us) / period_us + 1;
    };
    return Due{sent_by(from_us), sent_by(to_us)};
}

int32_t feet_of(double millimetres) {
    return static_cast<int32_t>(std::lround(millimetres / 1000.0 * kFeetPerMetre));
}

}  // namespace

void Sky::fly_1090(uint64_t now_us, const AdsbDatum& datum, models::Lr2021& radio) {
    if (squittering_)
        for (const VirtualAircraft& aircraft : aircraft_)
            if (aircraft.used && aircraft.adsb.fitted)
                squitter(aircraft, squittered_until_us_, now_us, datum, radio);
    squittering_ = true;
    squittered_until_us_ = now_us;
}

void Sky::squitter(const VirtualAircraft& aircraft, uint64_t from_us, uint64_t to_us,
                   const AdsbDatum& datum, models::Lr2021& radio) {
    const uint32_t phase = aircraft.address * 2654435761u;
    const double latitude = latitude_1e7_of(aircraft) / 1e7;
    const double longitude = longitude_1e7_of(aircraft) / 1e7;
    const double track_radians = aircraft.track_deg * kPi / 180.0;
    const AdsbOut& out = aircraft.adsb;
    const bool still = out.on_ground && aircraft.speed_mps <= 0;
    const uint8_t capability = out.on_ground ? kGroundCapability : kAirborneCapability;

    const uint64_t position_period_us = still ? kStillPositionPeriodUs : kSquitterPeriodUs;
    const Due positions = due(from_us, to_us, position_period_us, phase % position_period_us);
    for (uint64_t position = positions.first; position < positions.end; position++) {
        const bool odd = (position & 1u) != 0;
        if (out.on_ground) {
            const int32_t speed_mkt =
                static_cast<int32_t>(aircraft.speed_mps * kKnotsPerMps * 1000);
            AdsbFrame frame = adsb_surface_position(aircraft.address, capability, latitude,
                                                    longitude, odd, adsb_movement_code(speed_mkt));
            adsb_set_message_bits(frame, 13, 13, 1);
            adsb_set_message_bits(frame, 14, 20,
                                  static_cast<uint64_t>(aircraft.track_deg * 128 / 360) & 0x7F);
            send(frame, radio);
        } else {
            const double hae_mm = datum.own_hae_mm + aircraft.up_m * 1000;
            const double pressure_mm =
                hae_mm - datum.geoid_separation_mm + datum.pressure_offset_mm;
            send(adsb_airborne_position(aircraft.address, capability, latitude, longitude, odd,
                                        feet_of(pressure_mm)),
                 radio);
        }
    }

    const uint64_t velocity_phase_us = (phase + kSquitterPeriodUs / 2) % kSquitterPeriodUs;
    const Due velocities = due(from_us, to_us, kSquitterPeriodUs, velocity_phase_us);
    for (uint64_t velocity = velocities.first; !out.on_ground && velocity < velocities.end;
         velocity++) {
        const int32_t east_kt = static_cast<int32_t>(
            std::lround(aircraft.speed_mps * std::sin(track_radians) * kKnotsPerMps));
        const int32_t north_kt = static_cast<int32_t>(
            std::lround(aircraft.speed_mps * std::cos(track_radians) * kKnotsPerMps));
        const int32_t climb_fpm =
            static_cast<int32_t>(std::lround(aircraft.climb_mps * kFeetPerMinutePerMps));
        const int32_t gnss_minus_pressure_ft =
            feet_of(datum.geoid_separation_mm - datum.pressure_offset_mm);
        AdsbFrame frame = adsb_ground_velocity(aircraft.address, capability, east_kt, north_kt,
                                               climb_fpm, gnss_minus_pressure_ft);
        if (out.pressure_only) adsb_set_message_bits(frame, 49, 56, 0);
        send(frame, radio);
    }

    const uint64_t identification_period_us =
        still ? kStillIdentificationPeriodUs : kIdentificationPeriodUs;
    const Due identifications =
        due(from_us, to_us, identification_period_us, phase % identification_period_us);
    for (uint64_t identification = identifications.first; identification < identifications.end;
         identification++)
        send(adsb_identification(aircraft.address, capability, out.category_type_code, out.category,
                                 aircraft.callsign),
             radio);
}

void Sky::send(const AdsbFrame& frame, models::Lr2021& radio) {
    const AdsbAir air = adsb_air(frame);
    adsb_sent_++;
    if (radio.receive_air(air.chips, sizeof(air.chips), kSquitterLevelDbm)) adsb_heard_++;
}

}  // namespace skyblip::simulator
