// ADS-L 4 SRD-860 issue 2 Subpart C, the physical layer: the bands, the coding and the second's
// slots.
#include <cstdint>

#include "core/fec/manchester.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/timing/slot.h"
#include "core/timing/transmit.h"
#include "doctest/doctest.h"
#include "hardware/parts/sx1262/sx1262.h"

using namespace skyblip;

namespace {
uint16_t chips_of(uint8_t byte) {
    uint8_t coded[2] = {0, 0};
    fec::manchester_encode(&byte, 1, coded);
    return static_cast<uint16_t>((coded[0] << 8) | coded[1]);
}

}  // namespace

// INFO: fc 18sep26 prose, but its two bands are the tables below, and those are checked
TEST_CASE("ADS-L.4.SRD860.C.1: ADS-L runs on an M band of two channels and an O band" *
          doctest::skip()) {
    FAIL("prose: an introduction to the bands, whose parameters are C.2, C.3 and C.4");
}

TEST_CASE("ADS-L.4.SRD860.C.2: the M band is modulated as the table prints it") {
    CHECK(timing::kMband0Hz == 868200000u);
    CHECK(timing::kMband1Hz == 868400000u);
    CHECK(protocol::kMbandChannelBandwidthHz == 200000u);
    CHECK(protocol::kMbandChipRateBps == 100000u);
    CHECK(protocol::kMbandDeviationHz == 50000u);
    CHECK(protocol::kMbandGaussianBtE2 == 50u);
    CHECK(parts::sx::kResultingErpCentiDb <= 1400);
}

TEST_CASE("ADS-L.4.SRD860.C.2.1: a one is sent as 01 and a zero as 10") {
    CHECK(chips_of(0xFF) == 0x5555);
    CHECK(chips_of(0x00) == 0xAAAA);
    CHECK(chips_of(0x72) == 0x95A6);

    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    for (int i = 0; i < protocol::kAdslFrameBytes; i++) frame[i] = static_cast<uint8_t>(i * 7 + 3);
    uint8_t chips[2 * protocol::kAdslFrameBytes] = {0};
    fec::manchester_encode(frame, sizeof(frame), chips);
    uint8_t back[sizeof(frame)] = {0};
    uint8_t error[sizeof(frame)] = {0};
    fec::manchester_decode(chips, sizeof(frame), back, error);
    for (size_t i = 0; i < sizeof(frame); i++) {
        CHECK(back[i] == frame[i]);
        CHECK(error[i] == 0);
    }
}

// The radio sends eight repetitions; the last four and both 1001s are the leading sync byte.
TEST_CASE("ADS-L.4.SRD860.C.2.3: the preamble is at most twelve 01s and then two 1001s") {
    const uint8_t lead = static_cast<uint8_t>(protocol::kAdslSyncWord >> 24);
    CHECK(chips_of(lead) == 0x5599);

    const int radio_repetitions = parts::sx::kPreambleChips / 2;
    const int lead_repetitions = 4;
    CHECK(radio_repetitions == 8);
    CHECK(radio_repetitions + lead_repetitions >= 5);
    CHECK(radio_repetitions + lead_repetitions <= 12);
}

TEST_CASE("ADS-L.4.SRD860.C.2.4: the sync word is 0x72 0x4B, Manchester-coded") {
    CHECK(static_cast<uint8_t>(protocol::kAdslSyncWord >> 16) == 0x72);
    CHECK(static_cast<uint8_t>(protocol::kAdslSyncWord >> 8) == 0x4B);

    uint8_t chips[protocol::kTxChipBytes] = {0};
    uint8_t frame[protocol::kAdslFrameBytes] = {0};
    protocol::encode_mband(protocol::kAdslSyncWord, frame, sizeof(frame), chips);
    CHECK(chips[2] == static_cast<uint8_t>(chips_of(0x72) >> 8));
    CHECK(chips[3] == static_cast<uint8_t>(chips_of(0x72)));
    CHECK(chips[4] == static_cast<uint8_t>(chips_of(0x4B) >> 8));
    CHECK(chips[5] == static_cast<uint8_t>(chips_of(0x4B)));
}

TEST_CASE("ADS-L.4.SRD860.C.2.5: consecutive Traffic transmissions change frequency") {
    uint32_t previous = 0;
    for (uint32_t utc_s = 1785628800; utc_s < 1785628810; utc_s++) {
        const int slot = timing::Transmitter::slot_in(utc_s, /*airborne=*/true);
        const uint32_t frequency = timing::Scheduler::slot_frequency_hz(slot);
        CHECK((frequency == timing::kMband0Hz || frequency == timing::kMband1Hz));
        if (previous != 0) CHECK(frequency != previous);
        previous = frequency;
    }
}

// TODO: fc 18sep26 no O-band LDR: one radio, and the dwell map spends the second on M-band and HDR
TEST_CASE("ADS-L.4.SRD860.C.3: the O-band LDR channel is 38.4 kbps behind sync word 0xB4 0x2B" *
          doctest::skip()) {
    FAIL("O-band LDR is not implemented: nothing tunes 869.525 MHz at 38.4 kbps");
}

// TODO: fc 18sep26 no O-band LDR, so neither its sync word nor its preamble is ever sent
TEST_CASE("ADS-L.4.SRD860.C.3.1: the O-band LDR sync word is 0xB4 0x2B" * doctest::skip()) {
    FAIL("O-band LDR is not implemented: no sync word is armed for that channel");
}

// TODO: fc 18sep26 no O-band LDR, so nothing emits its 40 repetitions either
TEST_CASE("ADS-L.4.SRD860.C.3.2: the O-band LDR preamble is 10, forty times" * doctest::skip()) {
    FAIL("O-band LDR is not implemented: no preamble is emitted on that channel");
}

TEST_CASE("ADS-L.4.SRD860.C.4: the O-band HDR channel is modulated as the table prints it") {
    CHECK(timing::kObandHz == 869525000u);
    CHECK(protocol::kUplinkChannelBandwidthHz == 250000u);
    CHECK(protocol::kUplinkChipRateBps == 200000u);
    CHECK(protocol::kUplinkDeviationHz == 50000u);
    CHECK(protocol::kUplinkGaussianBtE2 == 50u);
    // GMSK is GFSK at modulation index 0.5, which is what the table's implicit deviation says.
    CHECK(4u * protocol::kUplinkDeviationHz == protocol::kUplinkChipRateBps);
}

TEST_CASE("ADS-L.4.SRD860.C.4.2: the O-band HDR preamble is 10, twelve times") {
    CHECK(protocol::kUplinkPreambleChips == 24u);
    CHECK(protocol::kUplinkPreambleChips / 2u == 12u);
}

TEST_CASE("ADS-L.4.SRD860.C.4.3: the O-band HDR sync word is 0x2D 0xD4, uncoded") {
    CHECK(protocol::kUplinkSync[0] == 0x2D);
    CHECK(protocol::kUplinkSync[1] == 0xD4);

    uint8_t frame[protocol::kUplinkFrameBytes] = {0};
    frame[0] = 0xA5;
    uint8_t out[protocol::kUplinkBurstBytes] = {0};
    CHECK(protocol::encode_oband(frame, out) == static_cast<size_t>(protocol::kUplinkBurstBytes));
    CHECK(out[0] == 0x2D);
    CHECK(out[1] == 0xD4);
    CHECK(out[3] == 0xA5);
}

TEST_CASE("ADS-L.4.SRD860.C.5: own-ship transmits inside the Direct slot and nowhere else") {
    CHECK(timing::kDirectStartMs == 450);
    CHECK(timing::kDirectEndMs == 1000);

    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;

    for (uint32_t address = 1; address < 64; address++) {
        timing::Transmitter tx;
        tx.configure(address * 0x9E37u);
        for (uint32_t utc_s = 0; utc_s < 32; utc_s++) {
            for (int slot = 0; slot < 2; slot++) {
                const timing::SlotPlan plan =
                    timing::Scheduler::plan(timing::Scheduler::slot_start(slot), clock);
                const timing::Transmitter::Attempt a = tx.attempt(plan, utc_s, 1000, true, 0);
                if (!a.go) continue;
                if (a.payload == timing::Transmitter::Payload::Callsign) continue;
                CHECK(timing::Scheduler::in_direct_slot(a.at_ms));
                CHECK(a.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                      timing::kDirectEndMs);
            }
        }
    }
}

// The one burst this device places outside the clause, stated here rather than
// discovered by a conformance bench: core/timing/README.md carries the argument.
TEST_CASE(
    "ADS-L.4.SRD860.C.5: the callsign burst is a deliberate deviation into the reserved 0-200") {
    timing::ClockState clock{};
    clock.utc_valid = true;
    clock.pps_locked = true;
    const timing::SlotPlan slot1 = timing::Scheduler::plan(timing::kSlot1StartMs, clock);

    int seen = 0;
    for (uint32_t address = 1; address < 64; address++) {
        timing::Transmitter tx;
        tx.configure(address * 0x9E37u);
        for (uint32_t utc_s = 0; utc_s < 32; utc_s++) {
            const timing::Transmitter::Attempt a = tx.attempt(slot1, utc_s, 1000, true, 0);
            if (!a.go || a.payload != timing::Transmitter::Payload::Callsign) continue;
            seen++;
            // Past the direct slot, inside slot 1's dwell, and complete before it ends.
            CHECK_FALSE(timing::Scheduler::in_direct_slot(a.at_ms));
            CHECK(a.at_ms >= timing::kCallsignStartMs);
            CHECK(a.at_ms + static_cast<int>(timing::Transmitter::kAirTimeMs) <=
                  timing::kCallsignEndMs);
            CHECK(a.frequency_hz == timing::kMband1Hz);
        }
    }
    CHECK(seen > 0);
}

TEST_CASE("ADS-L.4.SRD860.C.5: the Uplink slot is listened to on the O band, inside its edges") {
    CHECK(timing::kUplinkRxStartMs >= 200);
    CHECK(timing::kUplinkRxEndMs <= 450);
    for (int phase = timing::kUplinkRxStartMs; phase < timing::kUplinkRxEndMs; phase++) {
        CHECK(timing::Scheduler::band_at(phase) == timing::Band::O);
        CHECK(timing::Scheduler::frequency_hz_at(phase) == timing::kObandHz);
    }
    for (int phase = 0; phase < 200; phase++) CHECK_FALSE(timing::Scheduler::in_direct_slot(phase));
}
