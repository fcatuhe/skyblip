#include "core/comms/log_link.h"

#include "core/events/link.h"
#include "core/util/cbor.h"

namespace skyblip::comms {

namespace {

constexpr int kRecordBytes = static_cast<int>(flight::kLogRecordBytes);

bool named_store(cbor::Writer& writer, uint32_t key, LogStore store) {
    if (store == LogStore::None) return false;
    if (store != LogStore::Flights) writer.add_code(key, store);
    return true;
}

bool parse_store(const cbor::Reader& frame, uint32_t key, LogRequest& request) {
    const Result<uint32_t> code = frame.read_unsigned(key);
    if (code.status() == Status::NotFound) return true;
    if (!code.ok()) return false;
    for (LogStore store : {LogStore::Flights, LogStore::Diagnostics}) {
        if (code.value() != static_cast<uint32_t>(store)) continue;
        request.store = store;
        return true;
    }
    return false;
}

uint32_t clamped_chunk_count(uint32_t count) {
    if (count == 0) return 1;
    return count > kMaxLogReadChunks ? kMaxLogReadChunks : count;
}

LogCommand log_command(uint32_t code) {
    if (code == static_cast<uint32_t>(link::Command::List)) return LogCommand::List;
    if (code == static_cast<uint32_t>(link::Command::Read)) return LogCommand::Read;
    if (code == static_cast<uint32_t>(link::Command::Erase)) return LogCommand::Erase;
    return LogCommand::None;
}

bool read_if_present(const cbor::Reader& frame, uint32_t key, std::optional<uint32_t>& out) {
    const Result<uint32_t> value = frame.read_unsigned(key);
    if (value.status() == Status::NotFound) return true;
    if (!value.ok()) return false;
    out = value.value();
    return true;
}

LogRequest refused(LogRequest request, link::Reason reason) {
    request.command = LogCommand::None;
    request.reason = reason;
    return request;
}

}  // namespace

int log_records_per_chunk(int payload_bytes, LogStore store) {
    const int envelope =
        kLogChunkEnvelopeBytes + (store == LogStore::Flights ? 0 : kLogStoreFieldBytes);
    for (int records = kMaxLogRecordsPerChunk; records > 0; records--) {
        const int raw = records * kRecordBytes;
        if (envelope + cbor::head_bytes(static_cast<uint64_t>(raw)) + raw <= payload_bytes)
            return records;
    }
    return 0;
}

LogRequest parse_log_request(const events::RxFrame& frame) {
    LogRequest request{};
    if (frame.endpoint != events::Endpoint::Log) return request;
    request.link_session = frame.session_id;
    request.reason = link::Reason::UnknownCommand;

    const cbor::Reader reader(ConstByteSpan(frame.data.data(), frame.bytes));
    const Result<uint32_t> code = reader.read_unsigned(link::kCommandKey);
    if (!code.ok()) return request;

    const LogCommand command = log_command(code.value());
    if (command == LogCommand::None) return request;
    if (!reader.keys_within(link::host_keys(static_cast<link::Command>(code.value()))))
        return refused(request, link::Reason::Invalid);
    request.command = command;

    uint32_t store_key = 0;
    if (command == LogCommand::List) {
        store_key = link::list_key::kLog;
        std::optional<uint32_t> index;
        if (!read_if_present(reader, link::list_key::kIndex, index))
            return refused(request, link::Reason::Invalid);
        request.index_valid = index.has_value();
        request.index = index.value_or(0);
    } else if (command == LogCommand::Erase) {
        store_key = link::erase_key::kLog;
    } else {
        store_key = link::read_key::kLog;
        const Result<uint32_t> session = reader.read_unsigned(link::read_key::kSession);
        if (session.status() == Status::NotFound) return request;
        std::optional<uint32_t> from;
        std::optional<uint32_t> count;
        if (!session.ok() || !read_if_present(reader, link::read_key::kFrom, from) ||
            !read_if_present(reader, link::read_key::kCount, count))
            return refused(request, link::Reason::Invalid);
        request.session = session.value();
        request.from = from.value_or(0);
        request.count = count ? clamped_chunk_count(*count) : 1;
    }
    if (!parse_store(reader, store_key, request)) return refused(request, link::Reason::UnknownLog);
    request.understood = true;
    request.reason.reset();
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

int format_log_acknowledgement(uint8_t* out, int capacity, bool accepted,
                               std::optional<link::Reason> reason, LogStore store) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Log);
    if (!named_store(writer, link::log_key::kLog, store)) return 0;
    writer.add_boolean(link::log_key::kAcknowledge, accepted);
    if (reason) writer.add_code(link::log_key::kReason, *reason);
    return writer.finish();
}

int format_log_count(uint8_t* out, int capacity, uint32_t sessions, bool truncated,
                     LogStore store) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Log);
    if (!named_store(writer, link::log_key::kLog, store)) return 0;
    writer.add_boolean(link::log_key::kAcknowledge, true);
    writer.add_unsigned(link::log_key::kSessions, sessions);
    // The partition holds more flights than the index offers. Said out loud,
    // because a tablet that shows sixteen when there are twenty has lied.
    writer.add_boolean(link::log_key::kTruncated, truncated);
    return writer.finish();
}

int format_log_session(uint8_t* out, int capacity, uint32_t index, uint32_t count,
                       uint32_t session_id, uint32_t records, bool closed, bool truncated,
                       LogStore store) {
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Session);
    if (!named_store(writer, link::session_key::kLog, store)) return 0;
    writer.add_unsigned(link::session_key::kIndex, index);
    writer.add_unsigned(link::session_key::kOf, count);
    // The session's opening UTC second: its name, and the base every one of its
    // records is timed against.
    writer.add_unsigned(link::session_key::kSession, session_id);
    writer.add_unsigned(link::session_key::kRecords, records);
    // False means the log stops where the power did. The tablet says so instead
    // of presenting a truncated flight as a complete one.
    writer.add_boolean(link::session_key::kClosed, closed);
    // INFO: fc 20sep26 the session opened in a sector the ring recycled: a suffix, not the run
    writer.add_boolean(link::session_key::kTruncated, truncated);
    return writer.finish();
}

int format_log_chunk(uint8_t* out, int capacity, uint32_t session_id, uint32_t from,
                     const uint8_t* records, int record_count, bool ends_session, LogStore store) {
    if (record_count < 0 || record_count > kMaxLogRecordsPerChunk) return 0;
    cbor::Writer writer(out, capacity);
    writer.add_code(link::kCommandKey, link::Command::Chunk);
    if (!named_store(writer, link::chunk_key::kLog, store)) return 0;
    writer.add_unsigned(link::chunk_key::kSession, session_id);
    writer.add_unsigned(link::chunk_key::kFrom, from);
    writer.add_boolean(link::chunk_key::kEndsSession, ends_session);
    writer.add_bytes(link::chunk_key::kRecords,
                     ConstByteSpan(records, static_cast<size_t>(record_count * kRecordBytes)));
    return writer.finish();
}

}  // namespace skyblip::comms
