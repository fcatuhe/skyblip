// The log dialect: which store a command names, how a window of chunks is cut, what a reply says.
#include <cstring>

#include "core/comms/config.h"
#include "core/comms/log_link.h"
#include "core/diag/record.h"
#include "core/events/link.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "doctest/doctest.h"
#include "ports/link.h"

using namespace skyblip;

namespace {

events::RxFrame log_frame(const char* json) {
    events::RxFrame frame{};
    frame.endpoint = events::Endpoint::Log;
    frame.len = static_cast<uint16_t>(std::strlen(json));
    std::memcpy(frame.data.data(), json, frame.len);
    return frame;
}

}  // namespace

TEST_CASE("log link: the three commands, and nothing else") {
    auto framed = [](const char* json) {
        events::RxFrame frame{};
        frame.endpoint = events::Endpoint::Log;
        frame.len = static_cast<uint16_t>(std::strlen(json));
        std::memcpy(frame.data.data(), json, frame.len);
        return frame;
    };

    CHECK(comms::parse_log_request(framed("{\"cmd\":\"list\"}")).command ==
          comms::LogCommand::List);
    CHECK(comms::parse_log_request(framed("{\"cmd\":\"erase\"}")).command ==
          comms::LogCommand::Erase);

    const comms::LogRequest read =
        comms::parse_log_request(framed("{\"cmd\":\"read\",\"session\":1785628800,\"from\":170}"));
    CHECK(read.command == comms::LogCommand::Read);
    CHECK(read.session == 1785628800u);
    CHECK(read.from == 170u);

    // A read with no session names nothing, so it is not a read.
    CHECK_FALSE(comms::parse_log_request(framed("{\"cmd\":\"read\"}")).understood);
    CHECK_FALSE(comms::parse_log_request(framed("{\"cmd\":\"format\"}")).understood);

    // Config frames belong to the config service even if they reach this parser.
    events::RxFrame wrong = framed("{\"cmd\":\"list\"}");
    wrong.endpoint = events::Endpoint::Config;
    CHECK_FALSE(comms::parse_log_request(wrong).understood);
}

TEST_CASE("log link: base64 carries the raw record, padding and all") {
    char out[16];
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("Man"), 3, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TWFu") == 0);
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("Ma"), 2, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TWE=") == 0);
    CHECK(comms::base64_encode(reinterpret_cast<const uint8_t*>("M"), 1, out, sizeof(out)) == 4);
    CHECK(std::strcmp(out, "TQ==") == 0);
    // It refuses rather than truncates: a half-encoded chunk decodes to garbage.
    uint8_t big[64] = {0};
    CHECK(comms::base64_encode(big, sizeof(big), out, sizeof(out)) == -1);
}

TEST_CASE("log link: how many records ride in a chunk follows the payload, not a guess") {
    // 24 raw bytes are exactly 32 base64 characters and the envelope at its
    // widest is 83, so this arithmetic is exact rather than an estimate.
    // Nothing fits in what BLE merely guarantees; an iPhone carries three; the
    // 247-byte MTU the old fixed five was aimed at still carries five; and a
    // phone that negotiates the whole L2CAP MTU is not short-changed.
    CHECK(comms::log_records_per_chunk(ports::kMinimumLinkPayload) == 0);
    CHECK(comms::log_records_per_chunk(comms::kSmallestSupportedPayload) == 3);
    CHECK(comms::log_records_per_chunk(244) == 5);
    CHECK(comms::log_records_per_chunk(495) == comms::kLogRecordsPerChunkMax);

    // And a chunk really does fit the frame it was sized for, at the widest
    // session id and record index the partition can produce.
    uint8_t raw[comms::kLogChunkRawBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(0xA0 + i);
    const int payloads[3] = {comms::kSmallestSupportedPayload, 244, 495};
    for (int payload : payloads) {
        const int records = comms::log_records_per_chunk(payload);
        char buf[comms::kLogReplyCap];
        const int len =
            comms::format_log_chunk(buf, payload + 1, 4294967295u, 4294967295u, raw, records, true);
        CHECK(len > 0);
        CHECK(len <= payload);
        CHECK(std::strstr(buf, "\"cmd\":\"chunk\"") != nullptr);
        CHECK(std::strstr(buf, "\"eof\":true") != nullptr);
    }
}

TEST_CASE("log link: a reply that will not fit the frame is refused, never shortened") {
    // The writer leaves out a field that will not fit whole, so a cap too small
    // yields a short but perfectly valid object - a chunk with no "data" key, or
    // a session line with no record count. A tablet must not be handed either.
    uint8_t raw[comms::kLogChunkRawBytes] = {0};
    char buf[comms::kLogReplyCap];
    const int tiny = ports::kMinimumLinkPayload + 1;
    CHECK(comms::format_log_chunk(buf, tiny, 1785628800u, 0, raw, 1, false) == 0);
    CHECK(comms::format_log_session(buf, tiny, 0, 3, 1785628800u, 1700, false, false) == 0);
    CHECK(comms::format_log_count(buf, tiny, 3, false) == 0);
}

TEST_CASE("log link: the count comes first and says whether it is the whole truth") {
    char buf[comms::kLogReplyCap];
    CHECK(comms::format_log_count(buf, sizeof(buf), 3, false) > 0);
    CHECK(std::strstr(buf, "\"sessions\":3") != nullptr);
    CHECK(std::strstr(buf, "\"truncated\":false") != nullptr);
}

TEST_CASE("log link: a session line says how many records and whether the flight ended") {
    char buf[comms::kLogReplyCap];
    const int len =
        comms::format_log_session(buf, sizeof(buf), 0, 3, 1785628800u, 1700, false, false);
    CHECK(len > 0);
    CHECK(std::strstr(buf, "\"records\":1700") != nullptr);
    // The one thing a tablet must not hide: this flight stops where the power did.
    CHECK(std::strstr(buf, "\"closed\":false") != nullptr);
}

TEST_CASE("log link: a flight opened after January 2038 is read and named by its own second") {
    constexpr uint32_t kNewYear2040 = 2208988800u;
    const comms::LogRequest read =
        comms::parse_log_request(log_frame("{\"cmd\":\"read\",\"session\":2208988800}"));
    CHECK(read.understood);
    CHECK(read.session == kNewYear2040);

    char buf[comms::kLogReplyCap];
    REQUIRE(comms::format_log_session(buf, sizeof(buf), 0, 1, kNewYear2040, 10, true, false) > 0);
    CHECK(std::strstr(buf, "\"session\":2208988800,") != nullptr);
    const uint8_t raw[flight::kLogRecordBytes] = {0};
    REQUIRE(comms::format_log_chunk(buf, sizeof(buf), kNewYear2040, 0, raw, 1, true) > 0);
    CHECK(std::strstr(buf, "\"session\":2208988800,") != nullptr);
}

TEST_CASE("log link: an absent store is the flights every client asks for today") {
    const comms::LogRequest list = comms::parse_log_request(log_frame("{\"cmd\":\"list\"}"));
    CHECK(list.store == store::SectorOwner::Flights);
    CHECK(list.reason == nullptr);

    const comms::LogRequest read =
        comms::parse_log_request(log_frame("{\"cmd\":\"read\",\"session\":1785628800}"));
    CHECK(read.store == store::SectorOwner::Flights);
    CHECK(read.count == 1u);

    const comms::LogRequest diagnostics = comms::parse_log_request(
        log_frame("{\"cmd\":\"read\",\"log\":\"diagnostics\",\"session\":7}"));
    REQUIRE(diagnostics.understood);
    CHECK(diagnostics.store == store::SectorOwner::Diagnostics);
    CHECK(comms::parse_log_request(log_frame("{\"cmd\":\"erase\",\"log\":\"flights\"}")).store ==
          store::SectorOwner::Flights);
}

TEST_CASE("log link: a store the partition does not hold is refused by name") {
    const comms::LogRequest request = comms::parse_log_request(
        log_frame("{\"cmd\":\"read\",\"log\":\"telemetry\",\"session\":7}"));
    CHECK_FALSE(request.understood);
    REQUIRE(request.reason != nullptr);
    CHECK(std::strcmp(request.reason, "unknown_log") == 0);

    const comms::LogRequest empty =
        comms::parse_log_request(log_frame("{\"cmd\":\"list\",\"log\":\"\"}"));
    CHECK_FALSE(empty.understood);
    CHECK(std::strcmp(empty.reason, "unknown_log") == 0);

    const comms::LogRequest unknown = comms::parse_log_request(log_frame("{\"cmd\":\"format\"}"));
    CHECK(std::strcmp(unknown.reason, "unknown_cmd") == 0);
}

TEST_CASE("log link: a read asks for a window of chunks and gets them back to back") {
    const comms::LogRequest request = comms::parse_log_request(
        log_frame("{\"cmd\":\"read\",\"session\":1785628800,\"from\":24,\"count\":4}"));
    REQUIRE(request.understood);
    CHECK(request.count == 4u);

    const comms::LogWindow window = comms::plan_log_window(request.from, request.count, 12, 170);
    REQUIRE(window.chunks == 4);
    for (int nth = 0; nth < window.chunks; nth++) {
        const comms::LogChunkSpan span = window.at(nth);
        CHECK(span.from == 24u + static_cast<uint32_t>(nth) * 12u);
        CHECK(span.records == 12);
        CHECK_FALSE(span.eof);
    }
}

TEST_CASE("log link: a window stops at the end of the session rather than wrapping") {
    // 170 records, 12 to a chunk, asked from 156: 12 then 2, and the eight asked for are two.
    const comms::LogWindow window = comms::plan_log_window(156, comms::kLogReadChunksMax, 12, 170);
    REQUIRE(window.chunks == 2);
    CHECK(window.at(0).records == 12);
    CHECK_FALSE(window.at(0).eof);
    CHECK(window.at(1).from == 168u);
    CHECK(window.at(1).records == 2);
    CHECK(window.at(1).eof);
    // Nothing past the last chunk, whatever the window was asked for.
    CHECK(window.at(2).records == 0);

    const comms::LogWindow past = comms::plan_log_window(170, 4, 12, 170);
    CHECK(past.chunks == 0);
    // A link too narrow to carry one record is a window of nothing, not a division by zero.
    CHECK(comms::plan_log_window(0, 4, 0, 170).chunks == 0);
}

TEST_CASE("log link: a window is clamped so one fetch cannot monopolise the link") {
    CHECK(comms::parse_log_request(log_frame("{\"cmd\":\"read\",\"session\":1,\"count\":99}"))
              .count == comms::kLogReadChunksMax);
    CHECK(
        comms::parse_log_request(log_frame("{\"cmd\":\"read\",\"session\":1,\"count\":0}")).count ==
        1u);
    CHECK(comms::parse_log_request(log_frame("{\"cmd\":\"read\",\"session\":1,\"count\":-3}"))
              .count == 1u);

    const comms::LogRequest request = comms::parse_log_request(
        log_frame("{\"cmd\":\"read\",\"session\":1,\"from\":0,\"count\":4000}"));
    const comms::LogWindow window =
        comms::plan_log_window(request.from, request.count, comms::kLogRecordsPerChunkMax, 5000);
    // Eight chunks of twelve: 96 records, about 4 kB, a quarter second of notifications.
    CHECK(window.chunks == static_cast<int>(comms::kLogReadChunksMax));
    CHECK(window.at(window.chunks - 1).from == 84u);
}

TEST_CASE("log link: every reply names the store it answers for") {
    const store::SectorOwner diagnostics = store::SectorOwner::Diagnostics;
    uint8_t raw[comms::kLogChunkRawBytes] = {0};
    char buf[comms::kLogReplyCap];

    REQUIRE(comms::format_log_chunk(buf, sizeof(buf), 7, 0, raw, 3, false, diagnostics) > 0);
    CHECK(std::strstr(buf, "\"log\":\"diagnostics\"") != nullptr);
    REQUIRE(comms::format_log_ack(buf, sizeof(buf), false, "busy", diagnostics) > 0);
    CHECK(std::strstr(buf, "\"log\":\"diagnostics\"") != nullptr);
    REQUIRE(comms::format_log_count(buf, sizeof(buf), 2, false, diagnostics) > 0);
    CHECK(std::strstr(buf, "\"log\":\"diagnostics\"") != nullptr);
    REQUIRE(comms::format_log_session(buf, sizeof(buf), 0, 2, 7, 900, true, false, diagnostics) >
            0);
    CHECK(std::strstr(buf, "\"log\":\"diagnostics\"") != nullptr);

    // Absent is flights on the reply as in the request, so a client that predates the field reads
    // the bytes it always read.
    REQUIRE(comms::format_log_chunk(buf, sizeof(buf), 7, 0, raw, 3, false) > 0);
    CHECK(std::strstr(buf, "\"log\"") == nullptr);

    // A free sector owns no records, so no reply can answer for it.
    CHECK(comms::format_log_chunk(buf, sizeof(buf), 7, 0, raw, 3, false,
                                  store::SectorOwner::None) == 0);
}

TEST_CASE("log link: a diagnostics chunk pays for its name out of the records it carries") {
    const store::SectorOwner diagnostics = store::SectorOwner::Diagnostics;
    CHECK(comms::log_records_per_chunk(244) == 5);
    CHECK(comms::log_records_per_chunk(244, diagnostics) == 4);
    // At the widest ATT_MTU the twelve-record ceiling is reached either way.
    CHECK(comms::log_records_per_chunk(495, diagnostics) == comms::kLogRecordsPerChunkMax);
    CHECK(comms::log_records_per_chunk(comms::kSmallestSupportedPayload, diagnostics) == 2);

    uint8_t raw[comms::kLogChunkRawBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(0xA0 + i);
    const int payloads[3] = {comms::kSmallestSupportedPayload, 244, 495};
    for (int payload : payloads) {
        char buf[comms::kLogReplyCap];
        const int len = comms::format_log_chunk(buf, payload + 1, 4294967295u, 4294967295u, raw,
                                                comms::log_records_per_chunk(payload, diagnostics),
                                                true, diagnostics);
        CHECK(len > 0);
        CHECK(len <= payload);
    }
}

// The chunking, the base64 and the resumable offload are the flights store's and shared as they
// stand: a diagnostics record that stopped being 24 bytes would silently break every chunk.
TEST_CASE("log link: a diagnostics record is the same 24 bytes a chunk is cut for") {
    CHECK(diag::kRecordBytes == flight::kLogRecordBytes);
}
