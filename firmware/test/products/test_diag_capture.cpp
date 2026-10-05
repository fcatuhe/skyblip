// The capture end to end: what arming writes, what the floor refuses, what reads back.
#include <cstring>
#include <string>

#include "core/diag/payload.h"
#include "core/events/link.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/skyblip_go/services/capture.h"
#include "products/skyblip_go/services/flight_log.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/capture_rig.h"
#include "test/support/glass_text.h"
#include "test/support/log_transfer.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using skyblip::reads_in;

namespace {

std::string last_log(Rig& rig) {
    const auto& sent = rig.platform.link().sent;
    for (size_t i = sent.size(); i > 0; i--)
        if (sent[i - 1].endpoint == events::Endpoint::Log) return sent[i - 1].bytes;
    return "";
}

std::string ask(Rig& rig, uint32_t& t, const char* json) {
    rig.platform.link().clear();
    rig.send_log(json);
    rig.run(t, t + 200);
    t += 200;
    return last_log(rig);
}

struct Capture {
    uint32_t records{0};
    int chunks{0};
    bool eof{false};
    bool decoded{true};
    int gaps{0};
    int flights{0};
};

Capture offload_capture(Rig& rig, uint32_t& t, uint32_t session) {
    Capture walk{};
    uint32_t from = 0;
    while (!walk.eof && walk.chunks < 400) {
        char command[128];
        std::snprintf(command, sizeof(command),
                      "{\"cmd\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":%u}",
                      session, from);
        const std::string chunk = ask(rig, t, command);
        if (chunk.find("\"cmd\":\"chunk\"") == std::string::npos) {
            walk.decoded = false;
            return walk;
        }
        if (field(chunk, "log") != "diagnostics") walk.decoded = false;

        const int n = std::atoi(field(chunk, "n").c_str());
        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = base64_decode(field(chunk, "data"), raw, sizeof(raw));
        if (bytes != n * static_cast<int>(diag::kRecordBytes)) walk.decoded = false;
        for (int i = 0; i < n; i++) {
            diag::Record record{};
            if (diag::decode_record(raw + i * diag::kRecordBytes, record) != Status::Ok) {
                walk.decoded = false;
                return walk;
            }
            if (record.type == diag::Type::Gap) walk.gaps++;
            if (record.type == diag::Type::Flight) walk.flights++;
            walk.records++;
        }
        from += static_cast<uint32_t>(n);
        walk.eof = field(chunk, "eof") == "true";
        walk.chunks++;
    }
    return walk;
}

void arm_the_power_run(Rig& rig, uint32_t& t) {
    open_capture_page(rig, t);
    rig.tap(t);
    rig.run(t, t + 200);
    t += 200;
    rig.double_press(t);
    rig.run(t, t + 200);
    t += 200;
}

}  // namespace

TEST_CASE("capture: the diagnostics page states the price, and one press does not arm it") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);

    open_capture_page(rig, t);
    const bus::CaptureState& price = rig.state().capture;
    CHECK(price.available);
    CHECK(price.pool_sectors == platform::host::FlashRegion::kSectorCount);
    CHECK(price.price_sectors == platform::host::FlashRegion::kSectorCount);
    CHECK(price.keeps_s > 0);
    CHECK(reads_in(rig.product.screen().framebuffer(), "TAKES 298 OF 298 SECTORS", 0, 0, 200, 200));
    CHECK(reads_in(rig.product.screen().framebuffer(), "PRESS TWICE TO ARM", 0, 0, 200, 200));

    rig.press(t);
    rig.run(t, t + 2000);
    t += 2000;
    CHECK_FALSE(rig.product.diag().armed());

    rig.double_press(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.diag().armed());
    CHECK(rig.product.diag().profile() == diag::Profile::Full);
    CHECK(rig.product.capture().capturing());
    CHECK(reads_in(rig.product.screen().framebuffer(), "STATE ARMED FULL", 0, 0, 200, 200));
    CHECK(reads_in(rig.product.screen().framebuffer(), "PRESS TWICE TO STOP", 0, 0, 200, 200));
}

TEST_CASE("capture: the page prices every capture, and the pad picks between them") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    open_capture_page(rig, t);

    const go::CaptureService& capture = rig.product.capture();
    CHECK(capture.keeps_s(diag::Profile::Full) > 0);
    CHECK(capture.keeps_s(diag::Profile::PowerRun) > 100 * capture.keeps_s(diag::Profile::Full));
    CHECK(capture.keeps_s(diag::Profile::FlightRun) == capture.keeps_s(diag::Profile::PowerRun));

    const go::Glass& glass = rig.product.screen().framebuffer();
    CHECK(reads_in(glass, "FULL", 0, 0, 200, 200, 1, /*ink=*/false));
    CHECK(reads_in(glass, "POWER RUN", 0, 0, 200, 200));

    rig.tap(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.screen().page() == go::Page::Capture);
    CHECK(reads_in(glass, "POWER RUN", 0, 0, 200, 200, 1, /*ink=*/false));
    CHECK(reads_in(glass, "FULL", 0, 0, 200, 200));

    rig.tap(t);
    rig.run(t, t + 1200);
    t += 1200;
    CHECK(rig.product.screen().page() == go::Page::Capture);
    CHECK(reads_in(glass, "FLIGHT RUN", 0, 0, 200, 200, 1, /*ink=*/false));
}

TEST_CASE("capture: the double press arms the capture in focus, and no other") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_the_power_run(rig, t);

    CHECK(rig.product.diag().armed());
    CHECK(rig.product.diag().profile() == diag::Profile::PowerRun);
    CHECK(rig.product.capture().capturing());

    taxi(rig, t, 5);
    CHECK(reads_in(rig.product.screen().framebuffer(), "STATE ARMED POWER RUN", 0, 0, 200, 200));
    // A capture recording two subjects every 30 s outlasts one recording eleven a second.
    CHECK(rig.state().capture.keeps_s > 10 * rig.product.capture().keeps_s(diag::Profile::Full));
}

// The pad walks off the last capture rather than trapping the thumb on the page.
TEST_CASE("capture: a tap off the last capture hands the page back to the menu it came from") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    open_capture_page(rig, t);

    rig.tap(t);
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::Page::Capture);
    rig.tap(t);
    CHECK(rig.product.screen().page() == go::page_after(go::Page::Capture));
}

TEST_CASE("capture: what an armed device recorded comes back through the offload") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    REQUIRE(rig.product.diag().armed());
    const uint32_t session = rig.product.capture().session_id();
    CHECK(session >= Rig::kUtcBase);

    taxi(rig, t, 30);
    CHECK(rig.product.capture().records_written() > 10);
    CHECK(rig.product.capture().sectors_owned() * flight::kLogSlotsPerSector >=
          rig.product.capture().records_written());
    CHECK(rig.product.capture().stopped() == bus::CaptureStop::None);

    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    CHECK_FALSE(rig.product.diag().armed());
    CHECK_FALSE(rig.product.capture().capturing());
    const uint32_t written = rig.product.capture().records_written();

    const std::string count = ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "log") == "diagnostics");
    CHECK(field(count, "sessions") == "1");

    const std::string listed =
        ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "records") == std::to_string(written));
    CHECK(field(listed, "closed") == "true");

    const Capture walk = offload_capture(rig, t, session);
    CHECK(walk.decoded);
    CHECK(walk.eof);
    CHECK(walk.records == written);
    CHECK(walk.flights > 10);
}

TEST_CASE("capture: a windowed read answers the chunks it was asked for, and stops at eof") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    const uint32_t session = rig.product.capture().session_id();
    feed(rig, t, 60);
    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    const uint32_t written = rig.product.capture().records_written();
    REQUIRE(written > 40);

    rig.raise_link();
    const int per_chunk = comms::log_records_per_chunk(rig.platform.link().payload_bytes(),
                                                       comms::LogStore::Diagnostics);
    REQUIRE(per_chunk > 0);
    char command[128];
    std::snprintf(command, sizeof(command),
                  "{\"cmd\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":0,"
                  "\"count\":3}",
                  session);
    rig.platform.link().clear();
    rig.send_log(command);
    rig.run(t, t + 200);
    t += 200;

    int chunks = 0;
    uint32_t records = 0;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != events::Endpoint::Log) continue;
        chunks++;
        records += static_cast<uint32_t>(std::atoi(field(frame.bytes, "n").c_str()));
    }
    CHECK(chunks == 3);
    CHECK(records == static_cast<uint32_t>(3 * per_chunk));
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: both stores are offloaded in one session, each under its own selector") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    taxi(rig, t, 10);
    fly(rig, t, 60);
    taxi(rig, t, 40);
    const uint32_t flight = rig.product.flight_log().session_id();
    const uint32_t capture = rig.product.capture().session_id();
    REQUIRE(flight != capture);

    const std::string flights = ask(rig, t, "{\"cmd\":\"list\"}");
    CHECK(field(flights, "log") == "");
    CHECK(field(flights, "sessions") == "1");

    const std::string diagnostics = ask(rig, t, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(diagnostics, "log") == "diagnostics");
    CHECK(field(diagnostics, "sessions") == "1");

    char command[128];
    std::snprintf(command, sizeof(command), "{\"cmd\":\"read\",\"session\":%u,\"from\":0}", flight);
    const std::string flight_chunk = ask(rig, t, command);
    CHECK(field(flight_chunk, "log") == "");
    CHECK(field(flight_chunk, "session") == std::to_string(flight));

    const Capture walk = offload_capture(rig, t, capture);
    CHECK(walk.decoded);
    CHECK(walk.eof);
    CHECK(walk.records > 0);
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: a boot comes up disarmed even with a capture on the flash") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(flight, t, 5);
    arm_the_power_run(flight, t);
    REQUIRE(flight.product.diag().profile() == diag::Profile::PowerRun);
    taxi(flight, t, 20);
    const uint32_t session = flight.product.capture().session_id();
    const uint32_t written = flight.product.capture().records_written();
    REQUIRE(written > 0);
    REQUIRE(flight.product.diag().armed());

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rt = 100;
    taxi(rebooted, rt, 20);

    CHECK_FALSE(rebooted.product.diag().armed());
    CHECK(rebooted.product.diag().profile() == diag::Profile::Full);
    CHECK_FALSE(rebooted.product.capture().capturing());
    CHECK(rebooted.product.capture().records_written() == 0);

    const std::string count = ask(rebooted, rt, "{\"cmd\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "sessions") == "1");
    const Capture walk = offload_capture(rebooted, rt, session);
    CHECK(walk.decoded);
    CHECK(walk.records == written);
}

TEST_CASE("capture: records the ring had to drop reach the flash as a gap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    const uint32_t session = rig.product.capture().session_id();

    uint32_t refused = 0;
    for (int i = 0; i < diag::Recorder::kCapacity * 2; i++)
        if (!rig.product.diag().record(bench_record(Rig::kUtcBase + static_cast<uint32_t>(i))))
            refused++;
    CHECK(refused > 0);
    CHECK(rig.product.capture().records_dropped() == refused);

    taxi(rig, t, 5);
    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;

    const Capture walk = offload_capture(rig, t, session);
    CHECK(walk.decoded);
    CHECK(walk.gaps == 1);
}

// The queue dies with the rails: the drain has to run after the radio is asleep.
TEST_CASE("capture: what is still queued reaches the flash on the way to power off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);
    arm_from_the_page(rig, t);
    REQUIRE(rig.product.capture().capturing());
    const uint32_t session = rig.product.capture().session_id();

    // Stopped where own-ship may key the PA, so the ring still holds records.
    while (!rig.state().rf.plan.tx_allowed) {
        rig.platform.clock().set_millis(t);
        rig.product.step(t);
        t += 10;
    }
    for (int i = 0; i < 20; i++) rig.product.diag().record(bench_record(Rig::kUtcBase + i));
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += 10;
    const int queued = rig.product.diag().queued();
    REQUIRE(queued > 0);
    const uint32_t written = rig.product.capture().records_written();

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, t);
    rig.platform.clock().set_millis(t);
    rig.product.step(t);
    t += 10;
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);

    CHECK(rig.product.diag().queued() == 0);
    CHECK_FALSE(rig.product.capture().capturing());
    CHECK(rig.product.capture().records_written() >= written + static_cast<uint32_t>(queued));

    Rig rebooted;
    rebooted.platform.log_flash().restore(rig.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rt = 100;
    taxi(rebooted, rt, 5);
    const std::string listed =
        ask(rebooted, rt, "{\"cmd\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "closed") == "true");
    CHECK(field(listed, "truncated") == "false");
}

TEST_CASE("capture: a device with no partition offers no capture to arm") {
    constexpr ports::Capabilities kNoStorage = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Storage));
    Rig rig{kNoStorage};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t t = 100;
    taxi(rig, t, 5);

    open_capture_page(rig, t);
    CHECK_FALSE(rig.state().capture.available);
    CHECK(reads_in(rig.product.screen().framebuffer(), "NO FLASH FITTED", 0, 0, 200, 200));

    rig.double_press(t);
    rig.run(t, t + 1000);
    t += 1000;
    CHECK_FALSE(rig.product.diag().armed());
    CHECK_FALSE(rig.product.capture().capturing());
}
