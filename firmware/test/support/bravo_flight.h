// Harness, not a test: a paired Go flying from kFlyFromMs, and the frames its Bravo stub replays.
#ifndef SKYBLIP_TEST_SUPPORT_BRAVO_FLIGHT_H
#define SKYBLIP_TEST_SUPPORT_BRAVO_FLIGHT_H

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/gnss/nmea.h"
#include "core/settings/address.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/glass.h"
#include "test/support/glass_text.h"
#include "test/support/tango.h"

namespace skyblip {

constexpr uint32_t kFlyFromMs = 10000;
constexpr int32_t kSpeedMmS = 25000;
constexpr int32_t kAltitudeMslM = 900;
constexpr int32_t kAltitudeHaeM = kAltitudeMslM + gnss::kDefaultGeoidSeparationMm / 1000;
constexpr uint32_t kTarget = 0x4459A9;

// INFO: fc 12nov26 the second the rig's k-th fix names, and its edge (Rig::push_timed_fix)
inline uint32_t utc_of(uint32_t second) { return Rig::kBaseUtcS + second; }
inline uint32_t edge_ms_of(uint32_t second) { return kFlyFromMs + 1000 * second; }

inline int32_t latitude_north_of_own(uint32_t second, int32_t north_m) {
    return 485000000 + static_cast<int32_t>(second) * 3000 + north_m * 1000000 / 11132;
}

inline std::string heard_at(uint32_t second, uint16_t into_ms, int32_t north_m) {
    return "\"at_s\":" + std::to_string(utc_of(second)) +
           ",\"into_ms\":" + std::to_string(into_ms) +
           ",\"latitude_1e7\":" + std::to_string(latitude_north_of_own(second, north_m)) +
           ",\"longitude_1e7\":85000000";
}

inline std::string level_target(uint32_t second, uint16_t into_ms, int32_t north_m,
                                const std::string& more = "") {
    return "\"address\":" + std::to_string(kTarget) + "," + heard_at(second, into_ms, north_m) +
           ",\"altitude_m\":" + std::to_string(kAltitudeHaeM) +
           ",\"altitude_age_ms\":0,\"flight_state\":\"airborne\",\"flight_state_age_ms\":0" + more;
}

struct Flight {
    Tango tango;
    std::vector<uint16_t> tones{};

    explicit Flight(std::vector<BravoStub::Recorded> recording = {}, bool paired = true)
        : tango(std::move(recording)) {
        begin(paired);
    }

    Flight(std::vector<BravoStub::Recorded> recording, BarometerByHand barometer)
        : tango(std::move(recording), barometer) {
        begin(true);
    }

    Rig& go() { return tango.go; }
    bus::State& state() { return tango.go.state(); }

    void fly(uint32_t seconds) { go().seconds(tango.now_ms, seconds, kSpeedMmS, kAltitudeMslM); }

    const traffic::Target* target() {
        const int slot = state().traffic.find(settings::kIcaoAddressMappingTable, kTarget);
        return slot < 0 ? nullptr : state().traffic.at(slot);
    }

    const ui::Canvas& glass() { return go().product.screen().framebuffer(); }

    bool reads(const char* text, int scale = 1) {
        return reads_in(glass(), text, 0, 0, go::kGlassWidth, go::kGlassHeight, scale);
    }

   private:
    void begin(bool paired) {
        tango.bravo.health.own_icao_set = true;
        if (paired) tango.pair();
        go().long_touch(tango.now_ms);
        REQUIRE(go().product.screen().page() == go::Page::Radar);
        go().raise_link(1);
        REQUIRE(tango.now_ms < kFlyFromMs);
        go().run(tango.now_ms, kFlyFromMs - 50);
        tango.now_ms = kFlyFromMs;
        listen_for_tones();
    }

    void listen_for_tones() {
        const std::function<void(uint32_t)> world = go().world;
        go().world = [this, world](uint32_t at_ms) {
            const uint32_t heard = go().platform.annunciator().tone_commands();
            if (heard != tone_commands_) tones.push_back(go().platform.annunciator().pitch_hz());
            tone_commands_ = heard;
            world(at_ms);
        };
        tone_commands_ = go().platform.annunciator().tone_commands();
    }

    uint32_t tone_commands_{0};
};

inline std::vector<BravoStub::Recorded> named_target_every_second(uint32_t from, uint32_t to) {
    std::vector<BravoStub::Recorded> recording;
    for (uint32_t second = from; second < to; second++)
        recording.push_back({edge_ms_of(second) + 300,
                             level_target(second, 100, 1500, ",\"callsign\":\"DLH4AB\"")});
    return recording;
}

}  // namespace skyblip

#endif
