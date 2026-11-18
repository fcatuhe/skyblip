#include "core/diagnostics/payload.h"
#include "protocols/common/little_endian.h"

namespace skyblip::diagnostics {

namespace {

constexpr uint8_t kReceiverKindShift = 2;
constexpr uint8_t kReceiverKindMask = 0x03;
constexpr uint8_t kReceiverFlagStampValid = 1u << 4;
constexpr uint8_t kReceiverFlagIntervalSeenValid = 1u << 5;
constexpr uint8_t kUnknownBaud = 0xFF;
constexpr int kBaudCodes = 6;
constexpr uint32_t kBaudOfCode[kBaudCodes] = {4800, 9600, 19200, 38400, 57600, 115200};

uint8_t baud_code(uint32_t baud) {
    for (int code = 0; code < kBaudCodes; code++)
        if (kBaudOfCode[code] == baud) return static_cast<uint8_t>(code);
    return kUnknownBaud;
}

}  // namespace

// INFO: fc 18nov26 the baud byte is $PCAS01's own code, 0 for 4800 to 5 for 115200
Record record_of(const ports::GnssEvent& value, const Instant& at) {
    Record record = framed(Type::Receiver, at);
    record.payload[0] = baud_code(value.baud);
    record.payload[1] = static_cast<uint8_t>(value.command);
    record.payload[2] = static_cast<uint8_t>(value.verdict);
    record.payload[3] = static_cast<uint8_t>(value.stage);
    wire::put_uint32(record.payload + 4, value.at_ms);
    wire::put_uint16(record.payload + 8, value.interval_ms);
    wire::put_uint16(record.payload + 10, value.interval_seen_ms);
    wire::put_uint16(record.payload + 12,
                     static_cast<uint16_t>(value.stamp_ms < 0 ? 0 : value.stamp_ms));
    record.payload[14] = value.steps;
    record.payload[15] = value.heard;
    record.flags =
        static_cast<uint8_t>(record.flags | ((static_cast<uint8_t>(value.kind) & kReceiverKindMask)
                                             << kReceiverKindShift));
    set_flag(record.flags, kReceiverFlagStampValid, value.stamp_ms >= 0);
    set_flag(record.flags, kReceiverFlagIntervalSeenValid, value.interval_seen_ms != 0);
    return record;
}

bool read(const Record& record, ports::GnssEvent& out) {
    if (record.type != Type::Receiver) return false;
    out = ports::GnssEvent{};
    out.kind =
        static_cast<ports::GnssEventKind>((record.flags >> kReceiverKindShift) & kReceiverKindMask);
    out.baud = record.payload[0] < kBaudCodes ? kBaudOfCode[record.payload[0]] : 0;
    out.command = static_cast<ports::GnssCommand>(record.payload[1]);
    out.verdict = static_cast<ports::GnssVerdict>(record.payload[2]);
    out.stage = static_cast<ports::GnssConfigStage>(record.payload[3]);
    out.at_ms = wire::get_uint32(record.payload + 4);
    out.interval_ms = wire::get_uint16(record.payload + 8);
    out.interval_seen_ms =
        record.flagged(kReceiverFlagIntervalSeenValid) ? wire::get_uint16(record.payload + 10) : 0;
    out.stamp_ms = record.flagged(kReceiverFlagStampValid)
                       ? static_cast<int16_t>(wire::get_uint16(record.payload + 12))
                       : int16_t{-1};
    out.steps = record.payload[14];
    out.heard = record.payload[15];
    return true;
}

}  // namespace skyblip::diagnostics
