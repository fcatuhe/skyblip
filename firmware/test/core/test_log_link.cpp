// The log dialect: which store a command names, how a window of chunks is cut, what a reply says.
#include <cstring>
#include <string>

#include "core/comms/config.h"
#include "core/comms/link_registry.h"
#include "core/comms/log_link.h"
#include "core/diagnostics/record.h"
#include "core/events/link.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"
#include "core/util/cbor.h"
#include "doctest/doctest.h"
#include "ports/link.h"
#include "test/support/config_frame.h"

using namespace skyblip;

namespace {

events::RxFrame log_frame(const char* json) { return link_frame(events::Endpoint::Log, 0, json); }

template <class Fill>
events::RxFrame written_log_frame(link::Command command, Fill fill) {
    events::RxFrame frame{};
    frame.endpoint = events::Endpoint::Log;
    cbor::Writer writer(frame.data.data(), static_cast<int>(frame.data.size()));
    writer.add_code(link::kCommandKey, command);
    fill(writer);
    frame.bytes = static_cast<uint16_t>(writer.finish());
    return frame;
}

std::string text(const uint8_t* frame, int bytes) {
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

}  // namespace

TEST_CASE("log link: the three commands, and nothing else") {
    auto framed = [](const char* json) { return log_frame(json); };

    CHECK(comms::parse_log_request(framed("{\"command\":\"list\"}")).command ==
          comms::LogCommand::List);
    CHECK(comms::parse_log_request(framed("{\"command\":\"erase\"}")).command ==
          comms::LogCommand::Erase);

    const comms::LogRequest read = comms::parse_log_request(
        framed("{\"command\":\"read\",\"session\":1785628800,\"from\":170}"));
    CHECK(read.command == comms::LogCommand::Read);
    CHECK(read.session == 1785628800u);
    CHECK(read.from == 170u);

    // A read with no session names nothing, so it is not a read.
    CHECK_FALSE(comms::parse_log_request(framed("{\"command\":\"read\"}")).understood);
    CHECK_FALSE(comms::parse_log_request(framed("{\"command\":\"get\"}")).understood);
    CHECK_FALSE(comms::parse_log_request(framed("{\"command\":300}")).understood);

    // Config frames belong to the config service even if they reach this parser.
    events::RxFrame wrong = framed("{\"command\":\"list\"}");
    wrong.endpoint = events::Endpoint::Config;
    CHECK_FALSE(comms::parse_log_request(wrong).understood);
}

// A field of the wrong type used to read as absent, so a read from record "170" started at 0.
TEST_CASE(
    "log link: an index, a session, a from or a count of the wrong type is refused as invalid") {
    const events::RxFrame frames[] = {
        written_log_frame(
            link::Command::List,
            [](cbor::Writer& writer) { writer.add_text(link::list_key::kIndex, "1"); }),
        written_log_frame(
            link::Command::Read,
            [](cbor::Writer& writer) { writer.add_text(link::read_key::kSession, "7"); }),
        written_log_frame(link::Command::Read,
                          [](cbor::Writer& writer) {
                              writer.add_unsigned(link::read_key::kSession, 7);
                              writer.add_boolean(link::read_key::kFrom, true);
                          }),
        written_log_frame(link::Command::Read,
                          [](cbor::Writer& writer) {
                              writer.add_unsigned(link::read_key::kSession, 7);
                              writer.add_integer(link::read_key::kCount, -2);
                          }),
    };
    for (const events::RxFrame& frame : frames) {
        CAPTURE(
            link_text(std::string(reinterpret_cast<const char*>(frame.data.data()), frame.bytes)));
        const comms::LogRequest request = comms::parse_log_request(frame);
        CHECK_FALSE(request.understood);
        CHECK(request.command == comms::LogCommand::None);
        CHECK(request.reason == link::Reason::Invalid);
    }
}

TEST_CASE("log link: how many records ride in a chunk follows the payload, not a guess") {
    // (payload - 18-byte widest envelope - the byte string's 2- or 3-byte head) / 24, capped at 19
    CHECK(comms::log_records_per_chunk(ports::kMinimumLinkPayloadBytes) == 0);
    CHECK(comms::log_records_per_chunk(comms::kSmallestSupportedPayloadBytes) == 6);
    CHECK(comms::log_records_per_chunk(244) == 9);
    CHECK(comms::log_records_per_chunk(495) == comms::kMaxLogRecordsPerChunk);
    CHECK(comms::kMaxLogRecordsPerChunk == 19);

    // And a chunk really does fit the frame it was sized for, at the widest
    // session id and record index the partition can produce.
    uint8_t raw[comms::kLogChunkRawBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(0xA0 + i);
    const int payloads[3] = {comms::kSmallestSupportedPayloadBytes, 244, 495};
    for (int payload : payloads) {
        const int records = comms::log_records_per_chunk(payload);
        uint8_t buffer[comms::kLogReplyBytes];
        const int written =
            comms::format_log_chunk(buffer, payload, 4294967295u, 4294967295u, raw, records, true);
        CHECK(written > 0);
        CHECK(written <= payload);
        CHECK(payload - written < static_cast<int>(flight::kLogRecordBytes));
        const std::string chunk = text(buffer, written);
        CHECK(chunk.find("\"command\":\"chunk\"") == 1);
        CHECK(chunk.find("\"ends_session\":true") != std::string::npos);
    }
}

TEST_CASE("log link: a reply that will not fit the frame is refused, never shortened") {
    // Cut at its capacity, a chunk would carry no records and a session line no count.
    uint8_t raw[comms::kLogChunkRawBytes] = {0};
    uint8_t buffer[comms::kLogReplyBytes];
    const int tiny = ports::kMinimumLinkPayloadBytes;
    CHECK(comms::format_log_chunk(buffer, tiny, 1785628800u, 0, raw, 1, false) == 0);
    CHECK(comms::format_log_session(buffer, 12, 0, 3, 1785628800u, 1700, false, false) == 0);
    CHECK(comms::format_log_count(buffer, 6, 3, false) == 0);
}

TEST_CASE("log link: the count comes first and says whether it is the whole truth") {
    uint8_t buffer[comms::kLogReplyBytes];
    const int written = comms::format_log_count(buffer, sizeof(buffer), 3, false);
    CHECK(text(buffer, written) ==
          "{\"command\":\"log\",\"acknowledge\":true,\"sessions\":3,\"truncated\":false}");
}

TEST_CASE("log link: a session line says how many records and whether the flight ended") {
    uint8_t buffer[comms::kLogReplyBytes];
    const int written =
        comms::format_log_session(buffer, sizeof(buffer), 0, 3, 1785628800u, 1700, false, false);
    const std::string session = text(buffer, written);
    CHECK(session.find("\"records\":1700") != std::string::npos);
    // The one thing a tablet must not hide: this flight stops where the power did.
    CHECK(session.find("\"closed\":false") != std::string::npos);
}

TEST_CASE("log link: a flight opened after January 2038 is read and named by its own second") {
    constexpr uint32_t kNewYear2040 = 2208988800u;
    const comms::LogRequest read =
        comms::parse_log_request(log_frame("{\"command\":\"read\",\"session\":2208988800}"));
    CHECK(read.understood);
    CHECK(read.session == kNewYear2040);

    uint8_t buffer[comms::kLogReplyBytes];
    int written =
        comms::format_log_session(buffer, sizeof(buffer), 0, 1, kNewYear2040, 10, true, false);
    CHECK(text(buffer, written).find("\"session\":2208988800,") != std::string::npos);
    const uint8_t raw[flight::kLogRecordBytes] = {0};
    written = comms::format_log_chunk(buffer, sizeof(buffer), kNewYear2040, 0, raw, 1, true);
    CHECK(text(buffer, written).find("\"session\":2208988800,") != std::string::npos);
}

TEST_CASE("log link: an absent store is the flights every client asks for today") {
    const comms::LogRequest list = comms::parse_log_request(log_frame("{\"command\":\"list\"}"));
    CHECK(list.store == store::SectorOwner::Flights);
    CHECK_FALSE(list.reason.has_value());

    const comms::LogRequest read =
        comms::parse_log_request(log_frame("{\"command\":\"read\",\"session\":1785628800}"));
    CHECK(read.store == store::SectorOwner::Flights);
    CHECK(read.count == 1u);

    const comms::LogRequest diagnostics = comms::parse_log_request(
        log_frame("{\"command\":\"read\",\"log\":\"diagnostics\",\"session\":7}"));
    REQUIRE(diagnostics.understood);
    CHECK(diagnostics.store == store::SectorOwner::Diagnostics);
    CHECK(
        comms::parse_log_request(log_frame("{\"command\":\"erase\",\"log\":\"flights\"}")).store ==
        store::SectorOwner::Flights);
}

TEST_CASE("log link: a store the partition does not hold is refused by name") {
    const comms::LogRequest request =
        comms::parse_log_request(log_frame("{\"command\":\"read\",\"log\":7,\"session\":7}"));
    CHECK_FALSE(request.understood);
    CHECK(request.reason == link::Reason::UnknownLog);

    const comms::LogRequest none =
        comms::parse_log_request(log_frame("{\"command\":\"list\",\"log\":0}"));
    CHECK_FALSE(none.understood);
    CHECK(none.reason == link::Reason::UnknownLog);

    const comms::LogRequest named =
        comms::parse_log_request(log_frame("{\"command\":\"list\",\"log\":\"telemetry\"}"));
    CHECK_FALSE(named.understood);
    CHECK(named.reason == link::Reason::UnknownLog);

    const comms::LogRequest unknown = comms::parse_log_request(log_frame("{\"command\":99}"));
    CHECK(unknown.reason == link::Reason::UnknownCommand);
}

TEST_CASE("log link: a read asks for a window of chunks and gets them back to back") {
    const comms::LogRequest request = comms::parse_log_request(
        log_frame("{\"command\":\"read\",\"session\":1785628800,\"from\":24,\"count\":4}"));
    REQUIRE(request.understood);
    CHECK(request.count == 4u);

    const comms::LogWindow window = comms::plan_log_window(request.from, request.count, 12, 170);
    REQUIRE(window.chunks == 4);
    for (int chunk = 0; chunk < window.chunks; chunk++) {
        const comms::LogChunkSpan span = window.at(chunk);
        CHECK(span.from == 24u + static_cast<uint32_t>(chunk) * 12u);
        CHECK(span.records == 12);
        CHECK_FALSE(span.ends_session);
    }
}

TEST_CASE("log link: a window stops at the end of the session rather than wrapping") {
    // 170 records, 12 to a chunk, asked from 156: 12 then 2, and the eight asked for are two.
    const comms::LogWindow window = comms::plan_log_window(156, comms::kMaxLogReadChunks, 12, 170);
    REQUIRE(window.chunks == 2);
    CHECK(window.at(0).records == 12);
    CHECK_FALSE(window.at(0).ends_session);
    CHECK(window.at(1).from == 168u);
    CHECK(window.at(1).records == 2);
    CHECK(window.at(1).ends_session);
    // Nothing past the last chunk, whatever the window was asked for.
    CHECK(window.at(2).records == 0);

    const comms::LogWindow past = comms::plan_log_window(170, 4, 12, 170);
    CHECK(past.chunks == 0);
    // A link too narrow to carry one record is a window of nothing, not a division by zero.
    CHECK(comms::plan_log_window(0, 4, 0, 170).chunks == 0);
}

TEST_CASE("log link: a window is clamped so one fetch cannot monopolise the link") {
    CHECK(comms::parse_log_request(log_frame("{\"command\":\"read\",\"session\":1,\"count\":99}"))
              .count == comms::kMaxLogReadChunks);
    CHECK(comms::parse_log_request(log_frame("{\"command\":\"read\",\"session\":1,\"count\":0}"))
              .count == 1u);
    CHECK(comms::parse_log_request(log_frame("{\"command\":\"read\",\"session\":1,\"count\":-3}"))
              .count == 1u);

    const comms::LogRequest request = comms::parse_log_request(
        log_frame("{\"command\":\"read\",\"session\":1,\"from\":0,\"count\":4000}"));
    const comms::LogWindow window =
        comms::plan_log_window(request.from, request.count, comms::kMaxLogRecordsPerChunk, 5000);
    // Eight chunks of nineteen: 152 records, about 4 kB, a quarter second of notifications.
    CHECK(window.chunks == static_cast<int>(comms::kMaxLogReadChunks));
    CHECK(window.at(window.chunks - 1).from == 133u);
}

TEST_CASE("log link: every reply names the store it answers for") {
    const store::SectorOwner diagnostics = store::SectorOwner::Diagnostics;
    uint8_t raw[comms::kLogChunkRawBytes] = {0};
    uint8_t buffer[comms::kLogReplyBytes];

    int written = comms::format_log_chunk(buffer, sizeof(buffer), 7, 0, raw, 3, false, diagnostics);
    CHECK(text(buffer, written).find("\"log\":\"diagnostics\"") != std::string::npos);
    written = comms::format_log_acknowledgement(buffer, sizeof(buffer), false, link::Reason::Busy,
                                                diagnostics);
    CHECK(
        text(buffer, written) ==
        "{\"command\":\"log\",\"log\":\"diagnostics\",\"acknowledge\":false,\"reason\":\"busy\"}");
    written = comms::format_log_count(buffer, sizeof(buffer), 2, false, diagnostics);
    CHECK(text(buffer, written).find("\"log\":\"diagnostics\"") != std::string::npos);
    written =
        comms::format_log_session(buffer, sizeof(buffer), 0, 2, 7, 900, true, false, diagnostics);
    CHECK(text(buffer, written).find("\"log\":\"diagnostics\"") != std::string::npos);

    // Absent is flights on the reply as in the request.
    written = comms::format_log_chunk(buffer, sizeof(buffer), 7, 0, raw, 3, false);
    CHECK(text(buffer, written).find("\"log\"") == std::string::npos);

    // A free sector owns no records, so no reply can answer for it.
    CHECK(comms::format_log_chunk(buffer, sizeof(buffer), 7, 0, raw, 3, false,
                                  store::SectorOwner::None) == 0);
}

// A chunk's records are one byte string, read back whole and counted by its length.
TEST_CASE("log link: a chunk carries its records raw, and its length is their count") {
    uint8_t raw[3 * flight::kLogRecordBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(i);
    uint8_t buffer[comms::kLogReplyBytes];
    const int written =
        comms::format_log_chunk(buffer, sizeof(buffer), 1785628800u, 24, raw, 3, false);
    const cbor::Reader reader(ConstByteSpan(buffer, static_cast<size_t>(written)));
    uint8_t back[sizeof(raw)];
    CHECK(reader.read_bytes(link::chunk_key::kRecords, ByteSpan(back, sizeof(back))) == Status::Ok);
    CHECK(std::memcmp(back, raw, sizeof(raw)) == 0);
    CHECK(reader.read_unsigned(link::chunk_key::kFrom).value() == 24u);
    CHECK_FALSE(reader.read_boolean(link::chunk_key::kEndsSession).value());

    const int empty =
        comms::format_log_chunk(buffer, sizeof(buffer), 1785628800u, 170, nullptr, 0, true);
    CHECK(text(buffer, empty) ==
          "{\"command\":\"chunk\",\"session\":1785628800,\"from\":170,\"ends_session\":true,"
          "\"records\":\"\"}");
}

TEST_CASE("log link: a diagnostics chunk pays for its name out of the records it carries") {
    const store::SectorOwner diagnostics = store::SectorOwner::Diagnostics;
    CHECK(comms::log_records_per_chunk(244) == 9);
    CHECK(comms::log_records_per_chunk(244, diagnostics) == 9);
    CHECK(comms::log_records_per_chunk(495, diagnostics) == comms::kMaxLogRecordsPerChunk);
    CHECK(comms::log_records_per_chunk(comms::kSmallestSupportedPayloadBytes, diagnostics) == 6);
    // 164 bytes carry six flights records and five diagnostics ones: there the two bytes cost one.
    CHECK(comms::log_records_per_chunk(164) == 6);
    CHECK(comms::log_records_per_chunk(164, diagnostics) == 5);

    uint8_t raw[comms::kLogChunkRawBytes];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = static_cast<uint8_t>(0xA0 + i);
    const int payloads[3] = {comms::kSmallestSupportedPayloadBytes, 244, 495};
    for (int payload : payloads) {
        uint8_t buffer[comms::kLogReplyBytes];
        const int written = comms::format_log_chunk(
            buffer, payload, 4294967295u, 4294967295u, raw,
            comms::log_records_per_chunk(payload, diagnostics), true, diagnostics);
        CHECK(written > 0);
        CHECK(written <= payload);
    }
}

// The chunking and the resumable offload are the flights store's and shared as they
// stand: a diagnostics record that stopped being 24 bytes would silently break every chunk.
TEST_CASE("log link: a diagnostics record is the same 24 bytes a chunk is cut for") {
    CHECK(diagnostics::kRecordBytes == flight::kLogRecordBytes);
}
