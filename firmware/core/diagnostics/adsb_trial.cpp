#include "core/diagnostics/adsb_trial.h"

namespace skyblip::diagnostics {

// TODO: fc 13nov26 adsb trial #293, remove this file once the trial picks the LR2021 settings
Record record_of(const AdsbSlot& value, const Instant& at) {
    Record record = framed(Type::AdsbSlot, at);
    record.payload[0] = value.profile;
    put_int8(record.payload + 1, value.threshold_dbm);
    record.payload[2] = value.gain_step;
    record.payload[3] = value.boost;
    put_uint16(record.payload + 4, value.frames);
    put_uint16(record.payload + 6, value.crc);
    put_uint16(record.payload + 8, value.unsupported);
    put_uint16(record.payload + 10, value.folded);
    put_uint16(record.payload + 12, value.positions);
    record.payload[14] = value.aircraft;
    record.payload[15] = value.counted_s;
    set_flag(record.flags, kAdsbSlotFlagFrontEndRaw, value.front_end_raw);
    set_flag(record.flags, kAdsbSlotFlagManchester, value.manchester);
    set_flag(record.flags, kAdsbSlotFlagFifoDropped, value.fifo_dropped);
    return record;
}

Record record_of(const AdsbChips& value, const Instant& at) {
    Record record = framed(Type::AdsbChips, at);
    record.payload[0] = value.seq;
    put_int8(record.payload + 1, value.rssi_dbm);
    for (uint32_t i = 0; i < kAdsbChipsPerRecord; i++) record.payload[2 + i] = value.chips[i];
    set_flag(record.flags, kAdsbChipsFlagTail, value.tail);
    set_flag(record.flags, kAdsbChipsFlagRssiValid, value.rssi_valid);
    record.flags = static_cast<uint8_t>(
        record.flags |
        ((static_cast<uint8_t>(value.verdict) & kAdsbChipsVerdictMask) << kAdsbChipsVerdictShift));
    return record;
}

bool read(const Record& record, AdsbSlot& out) {
    if (record.type != Type::AdsbSlot) return false;
    out = AdsbSlot{};
    out.profile = record.payload[0];
    out.threshold_dbm = get_int8(record.payload + 1);
    out.gain_step = record.payload[2];
    out.boost = record.payload[3];
    out.frames = get_uint16(record.payload + 4);
    out.crc = get_uint16(record.payload + 6);
    out.unsupported = get_uint16(record.payload + 8);
    out.folded = get_uint16(record.payload + 10);
    out.positions = get_uint16(record.payload + 12);
    out.aircraft = record.payload[14];
    out.counted_s = record.payload[15];
    out.front_end_raw = record.flagged(kAdsbSlotFlagFrontEndRaw);
    out.manchester = record.flagged(kAdsbSlotFlagManchester);
    out.fifo_dropped = record.flagged(kAdsbSlotFlagFifoDropped);
    return true;
}

bool read(const Record& record, AdsbChips& out) {
    if (record.type != Type::AdsbChips) return false;
    out = AdsbChips{};
    out.seq = record.payload[0];
    out.rssi_dbm = get_int8(record.payload + 1);
    for (uint32_t i = 0; i < kAdsbChipsPerRecord; i++) out.chips[i] = record.payload[2 + i];
    out.tail = record.flagged(kAdsbChipsFlagTail);
    out.rssi_valid = record.flagged(kAdsbChipsFlagRssiValid);
    out.verdict = static_cast<AdsbChipsVerdict>((record.flags >> kAdsbChipsVerdictShift) &
                                                kAdsbChipsVerdictMask);
    return true;
}

}  // namespace skyblip::diagnostics
