// Harness, not a test: a fix record written whole, as the writer gets it from the pre-takeoff ring.
// The device never encodes a fix outside that ring.
#ifndef SKYBLIP_TEST_SUPPORT_LOG_RECORD_WRITE_H
#define SKYBLIP_TEST_SUPPORT_LOG_RECORD_WRITE_H

#include <cstdint>

#include "core/flight/log_record.h"

namespace skyblip::flight {

inline void encode_log_record(const LogRecord& record, uint32_t base_utc_s, uint8_t* out) {
    uint8_t body[kLogFixBodyBytes];
    encode_log_fix_body(record, body);
    seal_log_fix(body, record.utc_s, base_utc_s, out);
}

}  // namespace skyblip::flight

#endif
