#include "core/comms/log_link.h"

#include <cstring>

#include "core/events/link.h"
#include "core/util/json.h"

namespace skyblip::comms {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t non_negative(long value) { return value < 0 ? 0u : static_cast<uint32_t>(value); }

// INFO: fc 04aug26 json::Writer leaves out a field that will not fit whole, so a
// reply built into too small a capacity is short but valid - which is exactly the
// failure that must not reach a tablet. Every formatter here answers 0 instead,
// and the service counts it.
int finished(json::Writer& writer) {
    const int bytes = writer.finish();
    return writer.overflowed() ? 0 : bytes;
}

bool named_store(json::Writer& writer, LogStore store) {
    if (store == LogStore::None) return false;
    if (store != LogStore::Flights) writer.add_string("log", log_store_name(store));
    return true;
}

bool parse_store(const json::Reader& reader, LogRequest& request) {
    char name[16] = {0};
    if (!reader.read_string("log", name, sizeof(name))) return true;
    for (LogStore store : {LogStore::Flights, LogStore::Diagnostics}) {
        if (std::strcmp(name, log_store_name(store)) != 0) continue;
        request.store = store;
        return true;
    }
    return false;
}

uint32_t clamped_chunk_count(long value) {
    const uint32_t count = non_negative(value);
    if (count == 0) return 1;
    return count > kMaxLogReadChunks ? kMaxLogReadChunks : count;
}

}  // namespace

const char* log_store_name(LogStore store) {
    switch (store) {
        case LogStore::Flights: return "flights";
        case LogStore::Diagnostics: return "diagnostics";
        case LogStore::None: break;
    }
    return "";
}

int log_records_per_chunk(int payload_bytes, LogStore store) {
    const int envelope =
        kLogChunkEnvelopeBytes + (store == LogStore::Flights ? 0 : kLogStoreFieldBytes);
    const int room = payload_bytes - envelope;
    if (room < kLogChunkBase64PerRecord) return 0;
    const int records = room / kLogChunkBase64PerRecord;
    return records > kMaxLogRecordsPerChunk ? kMaxLogRecordsPerChunk : records;
}

int base64_encode(const uint8_t* data, int bytes, char* out, int capacity) {
    const int needed = ((bytes + 2) / 3) * 4;
    if (needed + 1 > capacity) return -1;
    int written = 0;
    for (int i = 0; i < bytes; i += 3) {
        const uint32_t first = data[i];
        const uint32_t second = i + 1 < bytes ? data[i + 1] : 0;
        const uint32_t third = i + 2 < bytes ? data[i + 2] : 0;
        const uint32_t triple = (first << 16) | (second << 8) | third;
        out[written++] = kAlphabet[(triple >> 18) & 0x3F];
        out[written++] = kAlphabet[(triple >> 12) & 0x3F];
        out[written++] = i + 1 < bytes ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out[written++] = i + 2 < bytes ? kAlphabet[triple & 0x3F] : '=';
    }
    out[written] = 0;
    return written;
}

LogRequest parse_log_request(const events::RxFrame& frame) {
    LogRequest request{};
    if (frame.endpoint != events::Endpoint::Log) return request;
    request.link_session = frame.session_id;
    request.reason = "unknown_cmd";

    json::Reader reader(reinterpret_cast<const char*>(frame.data.data()), frame.bytes);
    if (!parse_store(reader, request)) {
        request.reason = "unknown_log";
        return request;
    }
    char command[16] = {0};
    if (!reader.read_string("cmd", command, sizeof(command))) return request;

    if (std::strcmp(command, "list") == 0) {
        request.command = LogCommand::List;
        long value = 0;
        request.index_valid = reader.read_integer("index", value);
        request.index = request.index_valid ? non_negative(value) : 0;
    } else if (std::strcmp(command, "erase") == 0) {
        request.command = LogCommand::Erase;
    } else if (std::strcmp(command, "read") == 0) {
        request.command = LogCommand::Read;
        if (!reader.read_unsigned("session", request.session)) return request;
        long value = 0;
        request.from = reader.read_integer("from", value) ? non_negative(value) : 0;
        request.count = reader.read_integer("count", value) ? clamped_chunk_count(value) : 1;
    } else {
        return request;
    }
    request.understood = true;
    request.reason = nullptr;
    return request;
}

LogWindow plan_log_window(uint32_t from, uint32_t count, int records_per_chunk,
                          uint32_t session_records) {
    LogWindow window{};
    if (records_per_chunk <= 0 || from >= session_records) return window;
    window.from = from;
    window.session_records = session_records;
    window.records_per_chunk = records_per_chunk;

    const uint32_t per_chunk = static_cast<uint32_t>(records_per_chunk);
    const uint32_t to_the_end = (session_records - from + per_chunk - 1) / per_chunk;
    window.chunks = static_cast<int>(count < to_the_end ? count : to_the_end);
    return window;
}

LogChunkSpan LogWindow::at(int chunk) const {
    LogChunkSpan span{};
    if (chunk < 0 || chunk >= chunks) return span;
    const uint32_t per_chunk = static_cast<uint32_t>(records_per_chunk);
    span.from = from + static_cast<uint32_t>(chunk) * per_chunk;
    const uint32_t left = session_records - span.from;
    span.records = static_cast<int>(left < per_chunk ? left : per_chunk);
    span.ends_session = span.from + static_cast<uint32_t>(span.records) >= session_records;
    return span;
}

int format_log_acknowledgement(char* out, int capacity, bool accepted, const char* reason,
                               LogStore store) {
    json::Writer writer(out, capacity);
    writer.add_string("cmd", "log");
    if (!named_store(writer, store)) return 0;
    writer.add_boolean("ack", accepted);
    if (reason != nullptr) writer.add_string("reason", reason);
    return finished(writer);
}

int format_log_count(char* out, int capacity, uint32_t sessions, bool truncated, LogStore store) {
    json::Writer writer(out, capacity);
    writer.add_string("cmd", "log");
    if (!named_store(writer, store)) return 0;
    writer.add_boolean("ack", true);
    writer.add_integer("sessions", static_cast<long>(sessions));
    // The partition holds more flights than the index offers. Said out loud,
    // because a tablet that shows sixteen when there are twenty has lied.
    writer.add_boolean("truncated", truncated);
    return finished(writer);
}

int format_log_session(char* out, int capacity, uint32_t index, uint32_t count, uint32_t session_id,
                       uint32_t records, bool closed, bool truncated, LogStore store) {
    json::Writer writer(out, capacity);
    writer.add_string("cmd", "session");
    if (!named_store(writer, store)) return 0;
    writer.add_integer("index", static_cast<long>(index));
    writer.add_integer("of", static_cast<long>(count));
    // The session's opening UTC second: its name, and the base every one of its
    // records is timed against.
    writer.add_integer("session", session_id);
    writer.add_integer("records", static_cast<long>(records));
    // False means the log stops where the power did. The tablet says so instead
    // of presenting a truncated flight as a complete one.
    writer.add_boolean("closed", closed);
    // INFO: fc 20sep26 the session opened in a sector the ring recycled: a suffix, not the run
    writer.add_boolean("truncated", truncated);
    return finished(writer);
}

int format_log_chunk(char* out, int capacity, uint32_t session_id, uint32_t from,
                     const uint8_t* records, int record_count, bool ends_session, LogStore store) {
    char data[kLogChunkRawBytes * 4 / 3 + 8];
    const int encoded =
        base64_encode(records, record_count * static_cast<int>(flight::kLogRecordBytes), data,
                      static_cast<int>(sizeof(data)));
    if (encoded < 0) return 0;

    json::Writer writer(out, capacity);
    writer.add_string("cmd", "chunk");
    if (!named_store(writer, store)) return 0;
    writer.add_integer("session", session_id);
    writer.add_integer("from", static_cast<long>(from));
    writer.add_integer("n", record_count);
    writer.add_boolean("eof", ends_session);
    writer.add_string("data", data);
    return finished(writer);
}

}  // namespace skyblip::comms
