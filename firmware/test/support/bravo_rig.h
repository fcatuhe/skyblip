// Harness, not a test: skyBlip Bravo on the X1's host models, under a clock the case advances.
#ifndef SKYBLIP_TEST_SUPPORT_BRAVO_RIG_H
#define SKYBLIP_TEST_SUPPORT_BRAVO_RIG_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/events/link.h"
#include "core/gnss/nmea.h"
#include "hardware/platform/host/meshtracker_x1.h"
#include "products/skyblip_bravo/product.h"
#include "test/support/adsb_frames.h"
#include "test/support/config_frame.h"
#include "test/support/lr2021_rig.h"

namespace skyblip {

using Bravo = bravo::Product<platform::host::MeshtrackerX1>;

struct BravoRig {
    // 2026-08-02T00:00:00Z, as the Go's rig dates its fixes.
    static constexpr uint32_t kBaseUtcS = 1785628800;
    static constexpr int32_t kLatitude1e7 = 485000000;
    static constexpr int32_t kLongitude1e7 = 85000000;

    platform::host::MeshtrackerX1 platform;
    // INFO: fc 12nov26 the board probes its parts as it is built, so a case changes a model before
    bool prepared{true};
    Bravo product{platform};
    uint32_t now_ms{0};
    uint32_t utc_offset_s{0};
    uint32_t fix_updates{0};
    // INFO: fc 12nov26 off unless a case asks: the cases that push fixes on the bus want one source
    bool gnss_on_air{false};

    // INFO: fc 12nov26 the barometer at the fix's height, so an aircraft at kAltitudeFt is in the
    // gate
    static constexpr int32_t kOwnAltitudeMm = 500000;
    static constexpr int32_t kAltitudeFt = 1800;

    explicit BravoRig(ports::Capabilities fitted = platform::host::MeshtrackerX1::kFullyFitted)
        : platform(fitted) {
        platform.barometer().set_altitude_mm(kOwnAltitudeMm);
    }

    template <class Prepare>
    explicit BravoRig(Prepare prepare)
        : platform(platform::host::MeshtrackerX1::kFullyFitted),
          prepared((prepare(platform), true)) {
        platform.barometer().set_altitude_mm(kOwnAltitudeMm);
    }

    Status setup() { return product.setup(); }

    void step() {
        platform.clock().set_milliseconds(now_ms);
        if (gnss_on_air) platform.gnss().tick(now_ms);
        product.run_pass(now_ms);
    }

    void run_for(uint32_t span_ms, uint32_t step_ms = runtime::kServicePassCeilingMs) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += step_ms) {
            step();
            now_ms += step_ms;
        }
    }

    // A solution as the AG3335 driver publishes one, on the ground where the case puts it.
    void push_fix(int32_t speed_mm_s = 0) {
        gnss::GnssSolution solution{};
        solution.fix_valid = true;
        solution.utc_valid = true;
        solution.utc_s = kBaseUtcS + utc_offset_s;
        solution.latitude_1e7 = kLatitude1e7;
        solution.longitude_1e7 = kLongitude1e7;
        solution.altitude_msl_mm = kOwnAltitudeMm;
        solution.altitude_hae_mm = solution.altitude_msl_mm + gnss::kDefaultGeoidSeparationMm;
        solution.geoid_measured = true;
        solution.speed_mm_s = speed_mm_s;
        solution.track_cdeg = 0;
        solution.satellites = 10;
        solution.hdop_e2 = 100;
        solution.vdop_e2 = 150;
        solution.sentences = ++fix_updates;
        product.bus().gnss.push(solution);
    }

    void blind_fix() {
        gnss::GnssSolution solution{};
        solution.fix_valid = false;
        solution.sentences = ++fix_updates;
        product.bus().gnss.push(solution);
    }

    void seconds_with_fix(uint32_t seconds, int32_t speed_mm_s = 0) {
        for (uint32_t i = 0; i < seconds; i++) {
            push_fix(speed_mm_s);
            run_for(1000);
            utc_offset_s++;
        }
    }

    void seconds_blind(uint32_t seconds) {
        for (uint32_t i = 0; i < seconds; i++) {
            blind_fix();
            run_for(1000);
            utc_offset_s++;
        }
    }

    // A DF17 frame on the air at 1090 MHz, as the LR2021 model detects its preamble and sync.
    void hear(AdsbFrame frame, int8_t rssi_dbm = -70) {
        const AdsbAir air = adsb_air(frame);
        platform.radio().receive_air(air.chips, sizeof(air.chips), rssi_dbm);
        step();
    }

    // The two halves of a position the assembly needs, north of the rig's own fix.
    void hear_aircraft(uint32_t address, double north_m, int32_t altitude_ft = kAltitudeFt) {
        const double latitude = kLatitude1e7 / 1e7 + north_m / 111320.0;
        const double longitude = kLongitude1e7 / 1e7;
        hear(adsb_airborne_position(address, 0, latitude, longitude, false, altitude_ft));
        hear(adsb_airborne_position(address, 0, latitude, longitude, true, altitude_ft));
    }

    void press() {
        platform.set_button_down(true);
        run_for(100);
        platform.set_button_down(false);
        run_for(100);
    }

    void double_press() {
        press();
        press();
    }

    void hold_button(uint32_t held_ms) {
        platform.set_button_down(true);
        run_for(held_ms);
        platform.set_button_down(false);
    }

    void raise_link(uint16_t session_id = 1) { platform.link().raise_link(session_id); }
    bool link_up() { return product.config_link().config().link_up(); }

    void send(const char* json) {
        if (!platform.link().up()) raise_link();
        platform.link().push_rx(
            link_frame(events::Endpoint::Config, platform.link().session_id(), json));
    }

    std::string last_on(events::Endpoint endpoint) {
        const auto& sent = platform.link().sent;
        for (auto it = sent.rbegin(); it != sent.rend(); ++it)
            if (it->endpoint == endpoint) return link_text(it->bytes);
        return std::string();
    }

    std::vector<std::string> sentences() {
        std::string all;
        for (const auto& frame : platform.link().sent)
            if (frame.endpoint == events::Endpoint::Nmea) all += frame.bytes;
        std::vector<std::string> out;
        size_t at = 0;
        for (size_t end = all.find("\r\n"); end != std::string::npos;
             at = end + 2, end = all.find("\r\n", at))
            out.push_back(all.substr(at, end - at));
        return out;
    }

    int count_of(const char* kind) {
        int count = 0;
        for (const std::string& sentence : sentences())
            if (sentence.rfind(kind, 0) == 0) count++;
        return count;
    }

    std::string last_of(const char* kind) {
        std::string found;
        for (const std::string& sentence : sentences())
            if (sentence.rfind(kind, 0) == 0) found = sentence;
        return found;
    }

    bus::State& state() { return product.state(); }
    indication::Condition lamp() { return product.alarm().indicator_condition(); }
};

}  // namespace skyblip

#endif
