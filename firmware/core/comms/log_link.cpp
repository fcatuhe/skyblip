#include "core/comms/log_link.h"

#include <cstring>

#include "core/events/link.h"
#include "core/util/json_min.h"

namespace skyblip::comms {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t non_negative(long v) { return v < 0 ? 0u : static_cast<uint32_t>(v); }

// INFO: fc 04aug26 json::Writer leaves out a field that will not fit whole, so a
// reply built into too small a cap is short but valid - which is exactly the
// failure that must not reach a tablet. Every formatter here answers 0 instead,
// and the service counts it.
int finished(json::Writer& writer) {
    const int len = writer.finish();
    return writer.overflowed() ? 0 : len;
}

bool named_store(json::Writer& writer, LogStore store) {
    if (store == LogStore::None) return false;
    if (store != LogStore::Flights) writer.kv_str("log", log_store_name(store));
    return true;
}

bool parse_store(const json::Reader& reader, LogRequest& request) {
    char name[16] = {0};
    if (!reader.get_str("log", name, sizeof(name))) return true;
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
    return count > kLogReadChunksMax ? kLogReadChunksMax : count;
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
    return records > kLogRecordsPerChunkMax ? kLogRecordsPerChunkMax : records;
}

int base64_encode(const uint8_t* in, int len, char* out, int cap) {
    const int needed = ((len + 2) / 3) * 4;
    if (needed + 1 > cap) return -1;
    int n = 0;
    for (int i = 0; i < len; i += 3) {
        const uint32_t b0 = in[i];
        const uint32_t b1 = i + 1 < len ? in[i + 1] : 0;
        const uint32_t b2 = i + 2 < len ? in[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out[n++] = kAlphabet[(triple >> 18) & 0x3F];
        out[n++] = kAlphabet[(triple >> 12) & 0x3F];
        out[n++] = i + 1 < len ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out[n++] = i + 2 < len ? kAlphabet[triple & 0x3F] : '=';
    }
    out[n] = 0;
    return n;
}

LogRequest parse_log_request(const events::RxFrame& frame) {
    LogRequest request{};
    if (frame.endpoint != events::Endpoint::Log) return request;
    request.link_session = frame.session_id;
    request.reason = "unknown_cmd";

    json::Reader reader(reinterpret_cast<const char*>(frame.data.data()), frame.len);
    if (!parse_store(reader, request)) {
        request.reason = "unknown_log";
        return request;
    }
    char command[16] = {0};
    if (!reader.get_str("cmd", command, sizeof(command))) return request;

    if (std::strcmp(command, "list") == 0) {
        request.command = LogCommand::List;
        long value = 0;
        request.index_valid = reader.get_int("index", value);
        request.index = request.index_valid ? non_negative(value) : 0;
    } else if (std::strcmp(command, "erase") == 0) {
        request.command = LogCommand::Erase;
    } else if (std::strcmp(command, "read") == 0) {
        request.command = LogCommand::Read;
        if (!reader.get_uint("session", request.session)) return request;
        long value = 0;
        request.from = reader.get_int("from", value) ? non_negative(value) : 0;
        request.count = reader.get_int("count", value) ? clamped_chunk_count(value) : 1;
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

LogChunkSpan LogWindow::at(int nth) const {
    LogChunkSpan span{};
    if (nth < 0 || nth >= chunks) return span;
    const uint32_t per_chunk = static_cast<uint32_t>(records_per_chunk);
    span.from = from + static_cast<uint32_t>(nth) * per_chunk;
    const uint32_t left = session_records - span.from;
    span.records = static_cast<int>(left < per_chunk ? left : per_chunk);
    span.eof = span.from + static_cast<uint32_t>(span.records) >= session_records;
    return span;
}

int format_log_ack(char* buf, int cap, bool ok, const char* reason, LogStore store) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "log");
    if (!named_store(writer, store)) return 0;
    writer.kv_bool("ack", ok);
    if (reason != nullptr) writer.kv_str("reason", reason);
    return finished(writer);
}

int format_log_count(char* buf, int cap, uint32_t sessions, bool truncated, LogStore store) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "log");
    if (!named_store(writer, store)) return 0;
    writer.kv_bool("ack", true);
    writer.kv_int("sessions", static_cast<long>(sessions));
    // The partition holds more flights than the index offers. Said out loud,
    // because a tablet that shows sixteen when there are twenty has lied.
    writer.kv_bool("truncated", truncated);
    return finished(writer);
}

int format_log_session(char* buf, int cap, uint32_t index, uint32_t count, uint32_t session_id,
                       uint32_t records, bool closed, bool truncated, LogStore store) {
    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "session");
    if (!named_store(writer, store)) return 0;
    writer.kv_int("index", static_cast<long>(index));
    writer.kv_int("of", static_cast<long>(count));
    // The session's opening UTC second: its name, and the base every one of its
    // records is timed against.
    writer.kv_int("session", session_id);
    writer.kv_int("records", static_cast<long>(records));
    // False means the log stops where the power did. The tablet says so instead
    // of presenting a truncated flight as a complete one.
    writer.kv_bool("closed", closed);
    // INFO: fc 20sep26 the session opened in a sector the ring recycled: a suffix, not the run
    writer.kv_bool("truncated", truncated);
    return finished(writer);
}

int format_log_chunk(char* buf, int cap, uint32_t session_id, uint32_t from, const uint8_t* raw,
                     int record_count, bool eof, LogStore store) {
    char data[kLogChunkRawBytes * 4 / 3 + 8];
    const int encoded = base64_encode(raw, record_count * static_cast<int>(flight::kLogRecordBytes),
                                      data, static_cast<int>(sizeof(data)));
    if (encoded < 0) return 0;

    json::Writer writer(buf, cap);
    writer.kv_str("cmd", "chunk");
    if (!named_store(writer, store)) return 0;
    writer.kv_int("session", session_id);
    writer.kv_int("from", static_cast<long>(from));
    writer.kv_int("n", record_count);
    writer.kv_bool("eof", eof);
    writer.kv_str("data", data);
    return finished(writer);
}

}  // namespace skyblip::comms
