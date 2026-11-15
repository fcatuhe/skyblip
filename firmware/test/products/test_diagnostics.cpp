// The collector on the running product: each number comes off the same services and parts the
// silicon build runs.
#include <string>

#include "core/events/link.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/diagnostics.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

using Collector = go::DiagnosticsCollector<Go>;

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

const comms::Diagnostics& snapshot(Rig& rig) {
    return rig.product.config_link().config().diagnostics();
}

void pass(Rig& rig, Collector& collector, uint32_t& now_ms, uint32_t for_ms) {
    const uint32_t until = now_ms + for_ms;
    for (; now_ms <= until; now_ms += 50) {
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        collector.step(rig.product, now_ms);
    }
}

void fly(Rig& rig, Collector& collector, uint32_t& now_ms, uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; i++) {
        rig.push_timed_fix(50000, 500);
        pass(rig, collector, now_ms, 950);
        rig.utc_offset_s++;
    }
}

std::string config_frames(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    std::string joined;
    for (const auto& frame : sent)
        if (frame.endpoint == events::Endpoint::Config) joined += link_text(frame.bytes);
    return joined;
}

}  // namespace

TEST_CASE("diagnostics: the collector reads the device, subsystem by subsystem") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0;
    fly(rig, collector, now_ms, 12);
    const comms::Diagnostics& read = snapshot(rig);

    CHECK(read.gnss_baud == 9600);  // parts::L76k::kFactoryBaudRate, unless autobaud moved
    CHECK(read.gnss_identified);
    CHECK(std::string(read.gnss_firmware) == "URANUS5,V5.1.0.0");
    CHECK(read.gnss_fixes > 0);

    // The rig injects solutions on the bus while the part model says nothing, so the driver refuses
    // every poll.
    CHECK(read.gnss_reject == gnss::FixReject::MissingRmc);
    CHECK(read.gnss_rejected > 0);

    CHECK(read.noise_dbm < 0);
    CHECK(read.range_refused == 0);
    // The host executor drains the radio every pass and has no interrupt line to arm.
    CHECK_FALSE(read.dio1_armed);
    CHECK(read.dio1_edges == 0);
    CHECK(read.dio1_missed == 0);

    CHECK(read.reset == power::ResetReason::PowerOn);
    CHECK(read.supply_warnings == 0);
    CHECK(read.battery_implausible == 0);
    CHECK(read.tracked == 0);
}

// A POFCON firing and a gauge reading the sanity floor threw away: core/power counts both and acts
// on neither.
TEST_CASE("diagnostics: the faults nobody could see reach the snapshot") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0;
    pass(rig, collector, now_ms, 1000);

    rig.platform.system_power().supply_warning = true;
    rig.platform.battery().divider_mv = 900;
    pass(rig, collector, now_ms, 12000);

    CHECK(snapshot(rig).supply_warnings == 1);
    CHECK(snapshot(rig).battery_implausible > 0);
}

TEST_CASE("diagnostics: the air's four outcomes leave the collector as four numbers") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0;
    pass(rig, collector, now_ms, 1000);

    rig.state().air.rx_bad = 4;
    rig.state().air.rx_wait = 58;
    rig.state().air.rx_type = 19;
    rig.state().air.tx_lost = 2;
    pass(rig, collector, now_ms, 1100);

    CHECK(snapshot(rig).rx_bad == 4);
    CHECK(snapshot(rig).rx_wait == 58);
    CHECK(snapshot(rig).rx_type == 19);
    CHECK(snapshot(rig).tx_lost == 2);
}

// The link may ask at any instant, and a fix lost nine seconds ago is not news.
TEST_CASE("diagnostics: one refresh every second, and the link answers from it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0;

    pass(rig, collector, now_ms, 0);
    const uint32_t refreshes = snapshot(rig).refreshes;
    CHECK(refreshes == 1);

    pass(rig, collector, now_ms, 5000);
    CHECK(snapshot(rig).refreshes >= refreshes + 5);

    rig.raise_link();
    rig.send("{\"command\":\"diagnostics\"}");
    pass(rig, collector, now_ms, 200);
    CHECK(has(config_frames(rig), "\"group\":\"radio\""));
}

// The collector writes one half of the snapshot and the config service the other, and the link
// reads both.
TEST_CASE("diagnostics: the link answers what the collector and the config service read") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0;
    rig.platform.battery().divider_mv = 3812;
    fly(rig, collector, now_ms, 4);
    rig.raise_link();
    rig.platform.link().clear();
    pass(rig, collector, now_ms, 1000);
    rig.send("{\"command\":\"diagnostics\"}");
    pass(rig, collector, now_ms, 200);

    const std::string frames = config_frames(rig);
    REQUIRE_FALSE(frames.empty());
    const std::string percent = std::to_string(rig.state().power.battery.percent);
    CHECK(has(frames, ("\"percent\":" + percent).c_str()));
    const std::string fixes = std::to_string(rig.state().flight.gnss_solutions);
    CHECK(has(frames, ("\"fixes\":" + fixes).c_str()));
}

namespace {
// INFO: fc 14nov26 now_ms + for_ms is no bound across the wrap, so this walks a count of steps
void pass_across_wrap(Rig& rig, Collector& collector, uint32_t& now_ms, uint32_t for_ms) {
    for (uint32_t stepped = 0; stepped <= for_ms; stepped += 50) {
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        collector.step(rig.product, now_ms);
        now_ms += 50;
    }
}
}  // namespace

// The cadence is an unsigned difference and uptime_s is now_ms / 1000, so it restarts at zero.
TEST_CASE("diagnostics: the cadence and the uptime survive the 49.7-day wrap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    Collector collector;
    uint32_t now_ms = 0xFFFFFF00u;  // 256 ms short of the wrap

    pass_across_wrap(rig, collector, now_ms, 0);
    CHECK(snapshot(rig).uptime_s == 4294967u);

    const uint32_t refreshes_before = snapshot(rig).refreshes;
    pass_across_wrap(rig, collector, now_ms, 20000);
    CHECK(snapshot(rig).refreshes >= refreshes_before + 20);
    CHECK(snapshot(rig).uptime_s < 60u);
}
