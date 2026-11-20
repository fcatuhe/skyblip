#ifndef SKYBLIP_CORE_COMMS_LOG_LINK_H
#define SKYBLIP_CORE_COMMS_LOG_LINK_H

#include <optional>

#include "core/comms/link_registry.h"
#include "core/events/link.h"
#include "core/flight/log_record.h"
#include "core/store/sector.h"

namespace skyblip::comms {

enum class LogCommand : uint8_t { None, List, Read, Erase };

// INFO: fc 20sep26 the allocator's owners, so this dialect cannot name a ring the partition lacks
using LogStore = store::SectorOwner;

// INFO: fc 20sep26 eight 498-byte notifications is a quarter second of link at one per interval
constexpr uint32_t kMaxLogReadChunks = 8;

struct LogRequest {
    LogCommand command{LogCommand::None};
    LogStore store{LogStore::Flights};
    // INFO: fc 18sep26 session is the flight being read, peer is the app reading it.
    events::Peer peer{0, 0};
    uint32_t session{0};
    uint32_t from{0};
    uint32_t count{1};
    uint32_t index{0};
    bool index_valid{false};
    bool understood{false};
    std::optional<link::Reason> reason{};
};

LogRequest parse_log_request(const events::RxFrame& frame);

struct LogChunkSpan {
    uint32_t from{0};
    int records{0};
    bool ends_session{false};
};

struct LogWindow {
    uint32_t from{0};
    uint32_t session_end{0};
    int records_per_chunk{0};
    int chunks{0};

    LogChunkSpan at(int chunk) const;
};

LogWindow plan_log_window(uint32_t from, uint32_t count, int records_per_chunk,
                          uint32_t session_end);

// INFO: fc 04aug26 a buffer bound, not a promise: the largest chunk an ATT_MTU 498 link carries
constexpr int kLogReplyBytes = 512;

// INFO: cf 03aug26 records travel raw, CRC and all, so the host checks each end to end
// INFO: fc 04aug26 records per chunk follow the payload, nineteen being what ATT_MTU 498 reaches
constexpr int kMaxLogRecordsPerChunk = 19;
constexpr int kLogChunkRawBytes =
    kMaxLogRecordsPerChunk * static_cast<int>(flight::kLogRecordBytes);

// INFO: fc 20oct26 the widest chunk envelope, measured in README.md's How wide the frames are
constexpr int kLogChunkEnvelopeBytes = 18;

// INFO: fc 20sep26 the log key and the diagnostics code, the only store spelled on the wire
constexpr int kLogStoreFieldBytes = 2;

// INFO: fc 20nov26 zero means not one record fits, a refusal the caller counts
int log_records_per_chunk(int payload_bytes, LogStore store = LogStore::Flights);

// INFO: fc 20nov26 each returns the bytes written, or 0 when the frame does not fit whole
int format_log_acknowledgement(uint8_t* out, int capacity, bool accepted,
                               std::optional<link::Reason> reason,
                               LogStore store = LogStore::Flights);
int format_log_count(uint8_t* out, int capacity, uint32_t sessions, bool truncated,
                     LogStore store = LogStore::Flights);
int format_log_session(uint8_t* out, int capacity, uint32_t index, uint32_t count,
                       uint32_t session_id, uint32_t first, uint32_t records, bool closed,
                       bool truncated, bool recording, LogStore store = LogStore::Flights);
int format_log_chunk(uint8_t* out, int capacity, uint32_t session_id, uint32_t from,
                     const uint8_t* records, int record_count, bool ends_session,
                     LogStore store = LogStore::Flights);

}  // namespace skyblip::comms

#endif
