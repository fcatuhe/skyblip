// The capture end to end: what arming writes, what the floor refuses, what reads back.
#include <cstring>
#include <string>

#include "core/diagnostics/payload.h"
#include "core/events/link.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "products/common/services/capture.h"
#include "products/common/services/flight_log.h"
#include "products/skyblip_go/settings_store.h"
#include "test/support/capture_rig.h"
#include "test/support/glass_text.h"
#include "test/support/log_transfer.h"
#include "test/support/product_rig.h"

using namespace skyblip;
using skyblip::reads_in;

namespace {

std::string last_log(Rig& rig) {
    const platform::host::Link::Frame* frame = last_log_frame(rig);
    return frame == nullptr ? "" : frame->bytes;
}

std::string ask_frame(Rig& rig, uint32_t& now_ms, const char* json) {
    rig.platform.link().clear();
    rig.send_log(json);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;
    return last_log(rig);
}

std::string ask(Rig& rig, uint32_t& now_ms, const char* json) {
    return link_text(ask_frame(rig, now_ms, json));
}

struct Capture {
    uint32_t records{0};
    int chunks{0};
    bool ends_session{false};
    bool decoded{true};
    int gaps{0};
    int flights{0};
};

Capture offload_capture(Rig& rig, uint32_t& now_ms, uint32_t session) {
    Capture walk{};
    uint32_t from = 0;
    while (!walk.ends_session && walk.chunks < 1000) {
        char command[128];
        std::snprintf(command, sizeof(command),
                      "{\"command\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":%u}",
                      session, from);
        const std::string frame = ask_frame(rig, now_ms, command);
        const std::string chunk = link_text(frame);
        if (chunk.find("\"command\":\"chunk\"") == std::string::npos) {
            walk.decoded = false;
            return walk;
        }
        if (field(chunk, "log") != "diagnostics") walk.decoded = false;

        uint8_t raw[comms::kLogChunkRawBytes];
        const int bytes = chunk_records(frame, raw, sizeof(raw));
        if (bytes < 0 || bytes % static_cast<int>(diagnostics::kRecordBytes) != 0)
            walk.decoded = false;
        const int records = bytes / static_cast<int>(diagnostics::kRecordBytes);
        for (int i = 0; i < records; i++) {
            diagnostics::Record record{};
            if (diagnostics::decode_record(raw + i * diagnostics::kRecordBytes, record) !=
                Status::Ok) {
                walk.decoded = false;
                return walk;
            }
            if (record.type == diagnostics::Type::Gap) walk.gaps++;
            if (record.type == diagnostics::Type::Flight) walk.flights++;
            walk.records++;
        }
        from += static_cast<uint32_t>(records);
        walk.ends_session = field(chunk, "ends_session") == "true";
        walk.chunks++;
    }
    return walk;
}

}  // namespace

// A page reads the price before it asks, so a pilot who helps knows which flights it costs.
TEST_CASE("capture: the dump's capture group prices every capture, then names the one that runs") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);

    comms::Diagnostics price{};
    rig.product.capture().describe(price);
    CHECK(price.capture_available);
    CHECK_FALSE(price.capture_armed);
    CHECK(price.capture_takes_sectors == platform::host::FlashRegion::kSectorCount);
    CHECK(price.capture_keeps_full_s > 0);
    // A capture recording two subjects every 30 s outlasts one recording eleven a second.
    CHECK(price.capture_keeps_power_run_s > 100 * price.capture_keeps_full_s);
    CHECK(rig.product.capture().keeps_s(diagnostics::Profile::FlightRun) ==
          price.capture_keeps_power_run_s);

    arm_over_the_link(rig, now_ms, "power_run");
    CHECK(rig.product.diagnostics().profile() == diagnostics::Profile::PowerRun);
    CHECK(rig.product.capture().capturing());
    taxi(rig, now_ms, 5);
    comms::Diagnostics running{};
    rig.product.capture().describe(running);
    CHECK(running.capture_armed);
    CHECK(running.capture_profile == comms::CaptureProfile::PowerRun);
    CHECK(running.capture_session == rig.product.capture().session_id());
    CHECK(running.capture_records > 0);
}

TEST_CASE("capture: one press on the glass refuses the capture a page asked for") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    rig.send("{\"command\":\"capture\",\"profile\":\"full\"}");
    rig.run(now_ms, now_ms + 3000);
    now_ms += 3000;
    REQUIRE(rig.product.config_link().config().pending() == comms::Pending::Capture);
    rig.press(now_ms);
    rig.run(now_ms, now_ms + 2000);
    now_ms += 2000;
    CHECK_FALSE(rig.product.diagnostics().armed());
    CHECK_FALSE(rig.product.capture().capturing());
}

TEST_CASE("capture: what an armed device recorded comes back through the offload") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    arm_over_the_link(rig, now_ms);
    REQUIRE(rig.product.diagnostics().armed());
    const uint32_t session = rig.product.capture().session_id();
    CHECK(session >= Rig::kBaseUtcS);

    taxi(rig, now_ms, 30);
    CHECK(rig.product.capture().records_written() > 10);
    CHECK(rig.product.capture().sectors_owned() * flight::kLogSlotsPerSector >=
          rig.product.capture().records_written());
    CHECK(rig.product.capture().stopped() == bus::CaptureStop::None);

    stop_over_the_link(rig, now_ms);
    CHECK_FALSE(rig.product.diagnostics().armed());
    CHECK_FALSE(rig.product.capture().capturing());
    const uint32_t written = rig.product.capture().records_written();

    const std::string count = ask(rig, now_ms, "{\"command\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "log") == "diagnostics");
    CHECK(field(count, "sessions") == "1");

    const std::string listed =
        ask(rig, now_ms, "{\"command\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "records") == std::to_string(written));
    CHECK(field(listed, "closed") == "true");

    const Capture walk = offload_capture(rig, now_ms, session);
    CHECK(walk.decoded);
    CHECK(walk.ends_session);
    CHECK(walk.records == written);
    CHECK(walk.flights > 10);
}

TEST_CASE(
    "capture: a windowed read answers the chunks it was asked for, and stops at the session's "
    "end") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    arm_over_the_link(rig, now_ms);
    const uint32_t session = rig.product.capture().session_id();
    feed(rig, now_ms, 60);
    stop_over_the_link(rig, now_ms);
    const uint32_t written = rig.product.capture().records_written();
    REQUIRE(written > 40);

    rig.raise_link();
    const int per_chunk = comms::log_records_per_chunk(rig.platform.link().payload_bytes(),
                                                       comms::LogStore::Diagnostics);
    REQUIRE(per_chunk > 0);
    char command[128];
    std::snprintf(command, sizeof(command),
                  "{\"command\":\"read\",\"log\":\"diagnostics\",\"session\":%u,\"from\":0,"
                  "\"count\":3}",
                  session);
    rig.platform.link().clear();
    rig.send_log(command);
    rig.run(now_ms, now_ms + 200);
    now_ms += 200;

    int chunks = 0;
    uint32_t records = 0;
    for (const auto& frame : rig.platform.link().sent) {
        if (frame.endpoint != events::Endpoint::Log) continue;
        chunks++;
        uint8_t raw[comms::kLogChunkRawBytes];
        records += static_cast<uint32_t>(chunk_records(frame.bytes, raw, sizeof(raw)) /
                                         static_cast<int>(diagnostics::kRecordBytes));
    }
    CHECK(chunks == 3);
    CHECK(records == static_cast<uint32_t>(3 * per_chunk));
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: both stores are offloaded in one session, each under its own selector") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    arm_over_the_link(rig, now_ms);
    taxi(rig, now_ms, 10);
    fly(rig, now_ms, 60);
    taxi(rig, now_ms, 40);
    const uint32_t flight = rig.product.flight_log().session_id();
    const uint32_t capture = rig.product.capture().session_id();
    REQUIRE(flight != capture);

    const std::string flights = ask(rig, now_ms, "{\"command\":\"list\"}");
    CHECK(field(flights, "log") == "");
    CHECK(field(flights, "sessions") == "1");

    const std::string diagnostics =
        ask(rig, now_ms, "{\"command\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(diagnostics, "log") == "diagnostics");
    CHECK(field(diagnostics, "sessions") == "1");

    char command[128];
    std::snprintf(command, sizeof(command), "{\"command\":\"read\",\"session\":%u,\"from\":0}",
                  flight);
    const std::string flight_chunk = ask(rig, now_ms, command);
    CHECK(field(flight_chunk, "log") == "");
    CHECK(field(flight_chunk, "session") == std::to_string(flight));

    const Capture walk = offload_capture(rig, now_ms, capture);
    CHECK(walk.decoded);
    CHECK(walk.ends_session);
    CHECK(walk.records > 0);
    CHECK(rig.product.flight_log().link_drops() == 0);
}

TEST_CASE("capture: a boot comes up disarmed even with a capture on the flash") {
    Rig flight;
    REQUIRE(flight.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(flight, now_ms, 5);
    arm_over_the_link(flight, now_ms, "power_run");
    REQUIRE(flight.product.diagnostics().profile() == diagnostics::Profile::PowerRun);
    taxi(flight, now_ms, 20);
    const uint32_t session = flight.product.capture().session_id();
    const uint32_t written = flight.product.capture().records_written();
    REQUIRE(written > 0);
    REQUIRE(flight.product.diagnostics().armed());

    Rig rebooted;
    rebooted.platform.log_flash().restore(flight.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rebooted_now_ms = 100;
    taxi(rebooted, rebooted_now_ms, 20);

    CHECK_FALSE(rebooted.product.diagnostics().armed());
    CHECK(rebooted.product.diagnostics().profile() == diagnostics::Profile::Full);
    CHECK_FALSE(rebooted.product.capture().capturing());
    CHECK(rebooted.product.capture().records_written() == 0);

    const std::string count =
        ask(rebooted, rebooted_now_ms, "{\"command\":\"list\",\"log\":\"diagnostics\"}");
    CHECK(field(count, "sessions") == "1");
    const Capture walk = offload_capture(rebooted, rebooted_now_ms, session);
    CHECK(walk.decoded);
    CHECK(walk.records == written);
}

TEST_CASE("capture: records the ring had to drop reach the flash as a gap") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    arm_over_the_link(rig, now_ms);
    const uint32_t session = rig.product.capture().session_id();

    uint32_t refused = 0;
    for (int i = 0; i < diagnostics::Recorder::kMaxRecords * 2; i++)
        if (!rig.product.diagnostics().record(
                bench_record(Rig::kBaseUtcS + static_cast<uint32_t>(i))))
            refused++;
    CHECK(refused > 0);
    CHECK(rig.product.capture().records_dropped() == refused);

    taxi(rig, now_ms, 5);
    stop_over_the_link(rig, now_ms);

    const Capture walk = offload_capture(rig, now_ms, session);
    CHECK(walk.decoded);
    CHECK(walk.gaps == 1);
}

// The queue dies with the rails: the drain has to run after the radio is asleep.
TEST_CASE("capture: what is still queued reaches the flash on the way to power off") {
    Rig rig;
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);
    arm_over_the_link(rig, now_ms);
    REQUIRE(rig.product.capture().capturing());
    const uint32_t session = rig.product.capture().session_id();

    // Stopped where own-ship may key the PA, so the ring still holds records.
    while (!rig.state().rf.plan.tx_allowed) {
        rig.platform.clock().set_milliseconds(now_ms);
        rig.product.run_pass(now_ms);
        now_ms += 10;
    }
    for (int i = 0; i < 20; i++) rig.product.diagnostics().record(bench_record(Rig::kBaseUtcS + i));
    rig.platform.clock().set_milliseconds(now_ms);
    rig.product.run_pass(now_ms);
    now_ms += 10;
    const int queued = rig.product.diagnostics().queued();
    REQUIRE(queued > 0);
    const uint32_t written = rig.product.capture().records_written();

    rig.product.shutdown().request(power::ShutdownReason::LinkRequest, now_ms);
    rig.platform.clock().set_milliseconds(now_ms);
    rig.product.run_pass(now_ms);
    now_ms += 10;
    REQUIRE(rig.product.shutdown().phase() == power::ShutdownPhase::Parking);

    CHECK(rig.product.diagnostics().queued() == 0);
    CHECK_FALSE(rig.product.capture().capturing());
    CHECK(rig.product.capture().records_written() >= written + static_cast<uint32_t>(queued));

    Rig rebooted;
    rebooted.platform.log_flash().restore(rig.platform.log_flash().bytes());
    REQUIRE(rebooted.setup() == Status::Ok);
    uint32_t rebooted_now_ms = 100;
    taxi(rebooted, rebooted_now_ms, 5);
    const std::string listed = ask(rebooted, rebooted_now_ms,
                                   "{\"command\":\"list\",\"log\":\"diagnostics\",\"index\":0}");
    CHECK(field(listed, "session") == std::to_string(session));
    CHECK(field(listed, "closed") == "true");
    CHECK(field(listed, "truncated") == "false");
}

TEST_CASE("capture: a device with no partition refuses a capture as no_storage") {
    constexpr ports::Capabilities kNoStorage = static_cast<ports::Capabilities>(
        static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
        ~static_cast<uint32_t>(ports::Capability::Storage));
    Rig rig{kNoStorage};
    REQUIRE(rig.setup() == Status::Ok);
    uint32_t now_ms = 100;
    taxi(rig, now_ms, 5);

    rig.send("{\"command\":\"capture\",\"profile\":\"full\"}");
    rig.run(now_ms, now_ms + 1000);
    now_ms += 1000;
    CHECK_FALSE(rig.state().capture.available);
    std::string reply;
    for (const auto& frame : rig.platform.link().sent)
        if (frame.endpoint == events::Endpoint::Config) reply = link_text(frame.bytes);
    CHECK(reply ==
          "{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"no_storage\"}");
    CHECK_FALSE(rig.product.diagnostics().armed());
    CHECK_FALSE(rig.product.capture().capturing());
}
