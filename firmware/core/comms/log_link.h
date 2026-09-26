#ifndef SKYBLIP_CORE_COMMS_LOG_LINK_H
#define SKYBLIP_CORE_COMMS_LOG_LINK_H

#include "core/events/link.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"

namespace skyblip::comms {

enum class LogCommand : uint8_t { None, List, Read, Erase };

// INFO: fc 20sep26 the allocator's owners, so this dialect cannot name a ring the partition lacks
using LogStore = store::SectorOwner;

// INFO: fc 20sep26 eight 498-byte notifications is a quarter second of link at one per interval
constexpr uint32_t kLogReadChunksMax = 8;

struct LogRequest {
    LogCommand command{LogCommand::None};
    LogStore store{LogStore::Flights};
    // INFO: fc 18sep26 session is the flight being read, link_session is the app reading it.
    uint16_t link_session{0};
    uint32_t session{0};
    uint32_t from{0};
    uint32_t count{1};
    // A bare list asks how many flights there are; a list with an index asks
    // about one of them. One command, one frame, either way.
    uint32_t index{0};
    bool index_valid{false};
    bool understood{false};
    const char* reason{nullptr};
};

LogRequest parse_log_request(const events::RxFrame& frame);

const char* log_store_name(LogStore store);

struct LogChunkSpan {
    uint32_t from{0};
    int records{0};
    bool eof{false};
};

struct LogWindow {
    uint32_t from{0};
    uint32_t session_records{0};
    int records_per_chunk{0};
    int chunks{0};

    LogChunkSpan at(int nth) const;
};

LogWindow plan_log_window(uint32_t from, uint32_t count, int records_per_chunk,
                          uint32_t session_records);

// INFO: fc 04aug26 The widest frame this dialect can be asked to build, which is
// a buffer bound and not a promise: what actually goes out is cut to the payload
// ports::Link::payload_bytes() reports. Room for the largest chunk a link that
// negotiated ATT_MTU 498 can carry.
constexpr int kLogReplyCap = 512;

// INFO: cf 03aug26 Raw bytes are what travels, CRC and all, so the host verifies
// each record against the same checksum the flash holds - the transfer is checked
// end to end rather than hop by hop.
//
// INFO: fc 04aug26 How many of them ride in one chunk is derived from the
// negotiated payload, not fixed at five: five was chosen against a 256-byte
// buffer, which an iPhone's 182 bytes cannot carry and a large-MTU phone would be
// short-changed by. Twelve is the ceiling because that is what a 498-byte ATT_MTU
// reaches; the base64 of 24 raw bytes is exactly 32 characters, so the arithmetic
// below is exact rather than an estimate.
constexpr int kLogRecordsPerChunkMax = 12;
constexpr int kLogChunkRawBytes =
    kLogRecordsPerChunkMax * static_cast<int>(flight::kLogRecordBytes);
constexpr int kLogChunkBase64PerRecord = static_cast<int>(flight::kLogRecordBytes) / 3 * 4;

// The chunk envelope at its widest: the longest session id and record index the
// partition can produce, the two-digit count, and an empty data string.
constexpr int kLogChunkEnvelopeBytes = 83;

// INFO: fc 20sep26 `,"log":"diagnostics"`, the only store spelled on the wire
constexpr int kLogStoreFieldBytes = 20;

// How many records a chunk may carry over a link that negotiated this payload.
// Zero means not even one fits, which is a refusal for the caller to count.
int log_records_per_chunk(int payload_bytes, LogStore store = LogStore::Flights);

// Returns the number of characters written, excluding the terminator.
int format_log_ack(char* buf, int cap, bool ok, const char* reason,
                   LogStore store = LogStore::Flights);
int format_log_count(char* buf, int cap, uint32_t sessions, bool truncated,
                     LogStore store = LogStore::Flights);
int format_log_session(char* buf, int cap, uint32_t index, uint32_t count, uint32_t session_id,
                       uint32_t records, bool closed, bool truncated,
                       LogStore store = LogStore::Flights);
int format_log_chunk(char* buf, int cap, uint32_t session_id, uint32_t from, const uint8_t* raw,
                     int record_count, bool eof, LogStore store = LogStore::Flights);

// Standard base64, no padding omitted, no line breaks. Returns characters
// written, or -1 if they would not fit.
int base64_encode(const uint8_t* in, int len, char* out, int cap);

}  // namespace skyblip::comms

#endif
