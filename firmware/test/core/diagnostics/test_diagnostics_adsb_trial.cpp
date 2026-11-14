// The two records of the reception trial, through the 24 bytes a fetch reads.
#include <string>

#include "core/diagnostics/adsb_trial.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "test/support/diagnostics_round_trip.h"

using namespace skyblip;

namespace {

std::string hex_of(const diagnostics::Record& record) {
    uint8_t raw[diagnostics::kRecordBytes]{};
    diagnostics::encode_record(record, raw);
    char text[2 * diagnostics::kRecordBytes + 1]{};
    hex::write(ConstByteSpan(raw, sizeof(raw)), text);
    return text;
}

diagnostics::AdsbSlot slot() {
    diagnostics::AdsbSlot value{};
    value.profile = 1;
    value.threshold_dbm = -98;
    value.gain_step = 13;
    value.boost = 7;
    value.frames = 660;
    value.crc = 540;
    value.unsupported = 12;
    value.folded = 98;
    value.positions = 41;
    value.aircraft = 9;
    value.counted_s = 59;
    value.manchester = true;
    value.fifo_dropped = true;
    return value;
}

}  // namespace

TEST_CASE("adsb trial record: a slot comes back with its profile, its counts and its flags") {
    const diagnostics::AdsbSlot back = diagnostics_round_trip(slot());
    CHECK(back.profile == 1);
    CHECK(back.threshold_dbm == -98);
    CHECK(back.gain_step == 13);
    CHECK(back.boost == 7);
    CHECK(back.frames == 660);
    CHECK(back.crc == 540);
    CHECK(back.unsupported == 12);
    CHECK(back.folded == 98);
    CHECK(back.positions == 41);
    CHECK(back.aircraft == 9);
    CHECK(back.counted_s == 59);
    CHECK_FALSE(back.front_end_raw);
    CHECK(back.manchester);
    CHECK(back.fifo_dropped);
}

// scripts/test_adsb_trial.py reads these bytes.
TEST_CASE("adsb trial record: the slot and the chips are the bytes the host decoders read") {
    CHECK(hex_of(diagnostics::record_of(slot(), diagnostics_test_instant())) ==
          "161bce0140f9a16a019e0d0794021c020c0062002900093b");

    diagnostics::AdsbChips half{};
    half.seq = 7;
    half.rssi_dbm = -87;
    half.rssi_valid = true;
    half.verdict = diagnostics::AdsbChipsVerdict::Crc;
    for (uint32_t i = 0; i < diagnostics::kAdsbChipsPerRecord; i++)
        half.chips[i] = static_cast<uint8_t>(0xA0 + i);
    CHECK(hex_of(diagnostics::record_of(half, diagnostics_test_instant())) ==
          "171bce0140f9a16a07a9a0a1a2a3a4a5a6a7a8a9aaabacad");
    half.tail = true;
    half.verdict = diagnostics::AdsbChipsVerdict::Unsupported;
    const diagnostics::AdsbChips back = diagnostics_round_trip(half);
    CHECK(back.tail);
    CHECK(back.rssi_valid);
    CHECK(back.rssi_dbm == -87);
    CHECK(back.verdict == diagnostics::AdsbChipsVerdict::Unsupported);
    CHECK(back.chips[13] == 0xAD);
}
