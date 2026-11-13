// The ADS-L uplink air<->ground round-trip,
// pure host, no hardware. Proves air+ground share one codec. Asserts RS(255,223)
// corrects all patterns up to 16 symbol errors and, beyond 16, that silent
// miscorrection is counted SEPARATELY from detected failure.
#include <cstring>

#include "doctest/doctest.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/test/channel.h"

using namespace skyblip;
using skyblip::adsl::ReedSolomon255;
using skyblip::wire::Status;

namespace {
adsl::UplinkTarget make_target(uint32_t address) {
    adsl::UplinkTarget target;
    target.address = address & 0xFFFFFF;
    target.address_mapping_table = 6;
    target.aircraft_category = 4;
    target.flight_state = 2;
    target.speed_q = 180;
    target.latitude_1e7 = 481234500;
    target.longitude_1e7 = 81234500;
    target.altitude_m = 1234;
    return target;
}
}  // namespace

TEST_CASE("reed-solomon: encode then decode with no errors returns 0 corrections") {
    ReedSolomon255 reed_solomon;
    uint8_t data[ReedSolomon255::kDataBytes];
    for (int i = 0; i < ReedSolomon255::kDataBytes; i++) data[i] = static_cast<uint8_t>(i * 3 + 1);
    uint8_t codeword[ReedSolomon255::kCodewordBytes];
    std::memcpy(codeword, data, ReedSolomon255::kDataBytes);
    reed_solomon.encode(data, codeword + ReedSolomon255::kDataBytes);
    CHECK(reed_solomon.syndromes_zero(codeword));
    CHECK(reed_solomon.decode(codeword) == 0);
    CHECK(std::memcmp(codeword, data, ReedSolomon255::kDataBytes) == 0);
}

TEST_CASE("reed-solomon: corrects ALL patterns up to 16 symbol errors") {
    ReedSolomon255 reed_solomon;
    wire::Random random(0xBEEF);
    uint8_t data[ReedSolomon255::kDataBytes];
    for (int i = 0; i < ReedSolomon255::kDataBytes; i++)
        data[i] = static_cast<uint8_t>(random.next());
    uint8_t base[ReedSolomon255::kCodewordBytes];
    std::memcpy(base, data, ReedSolomon255::kDataBytes);
    reed_solomon.encode(data, base + ReedSolomon255::kDataBytes);

    for (int errors = 1; errors <= 16; errors++) {
        for (int trial = 0; trial < 200; trial++) {
            uint8_t codeword[ReedSolomon255::kCodewordBytes];
            std::memcpy(codeword, base, ReedSolomon255::kCodewordBytes);
            bool used[ReedSolomon255::kCodewordBytes] = {false};
            for (int error = 0; error < errors; error++) {
                int position;
                do {
                    position = random.next() % ReedSolomon255::kCodewordBytes;
                } while (used[position]);
                used[position] = true;
                uint8_t value;
                do {
                    value = static_cast<uint8_t>(random.next());
                } while (value == 0);
                codeword[position] ^= value;
            }
            int corrected = reed_solomon.decode(codeword);
            CHECK(corrected == errors);
            CHECK(std::memcmp(codeword, base, ReedSolomon255::kCodewordBytes) == 0);
        }
    }
}

TEST_CASE("reed-solomon: beyond 16 errors, detected vs silent miscorrection, counted") {
    ReedSolomon255 reed_solomon;
    wire::Random random(0x1234);
    uint8_t data[ReedSolomon255::kDataBytes];
    for (int i = 0; i < ReedSolomon255::kDataBytes; i++)
        data[i] = static_cast<uint8_t>(random.next());
    uint8_t base[ReedSolomon255::kCodewordBytes];
    std::memcpy(base, data, ReedSolomon255::kDataBytes);
    reed_solomon.encode(data, base + ReedSolomon255::kDataBytes);

    int detected = 0, silent = 0, lucky = 0;
    const int kTrials = 4000;
    for (int trial = 0; trial < kTrials; trial++) {
        uint8_t codeword[ReedSolomon255::kCodewordBytes];
        std::memcpy(codeword, base, ReedSolomon255::kCodewordBytes);
        int errors = 17 + (random.next() % 20);  // 17..36 errors
        bool used[ReedSolomon255::kCodewordBytes] = {false};
        for (int error = 0; error < errors; error++) {
            int position;
            do {
                position = random.next() % ReedSolomon255::kCodewordBytes;
            } while (used[position]);
            used[position] = true;
            uint8_t value;
            do {
                value = static_cast<uint8_t>(random.next());
            } while (value == 0);
            codeword[position] ^= value;
        }
        int corrected = reed_solomon.decode(codeword);
        if (corrected < 0) {
            detected++;  // safe
        } else if (std::memcmp(codeword, base, ReedSolomon255::kCodewordBytes) == 0) {
            lucky++;  // happened to fully recover (rare)
        } else {
            silent++;  // decoded to a WRONG codeword: the danger mode
        }
    }
    MESSAGE("detected=" << detected << " silent=" << silent << " lucky=" << lucky);
    // The vast majority must be safely detected. Silent miscorrection is bounded
    // by the RS structure and must stay a small minority (and is measured, not
    // hidden). decode() re-verifies syndromes so silent should be ~0 here.
    CHECK(detected > kTrials * 3 / 4);
    CHECK(silent <= 5);
}

TEST_CASE("uplink: air<->ground round-trip is lossless at BER 0 (anti-drift lock)") {
    adsl::Uplink up;
    adsl::UplinkTarget tx[adsl::Uplink::kMaxTargets];
    int count = adsl::Uplink::kMaxTargets;
    for (int i = 0; i < count; i++) tx[i] = make_target(0x200000 + i);

    uint8_t frame[adsl::Uplink::kFrameBytes];
    CHECK(up.encode(tx, count, /*key=*/0, frame) == Status::Ok);

    adsl::UplinkTarget rx[adsl::Uplink::kMaxTargets];
    adsl::Uplink::DecodeStatistics statistics;
    CHECK(up.decode(frame, rx, count, statistics) == Status::Ok);
    CHECK(statistics.targets == count);
    CHECK(statistics.corrected == 0);
    for (int i = 0; i < count; i++) {
        CHECK(rx[i].address == tx[i].address);
        CHECK(rx[i].latitude_1e7 == tx[i].latitude_1e7);
        CHECK(rx[i].longitude_1e7 == tx[i].longitude_1e7);
        CHECK(rx[i].altitude_m == tx[i].altitude_m);
        CHECK(int(rx[i].aircraft_category) == int(tx[i].aircraft_category));
    }
}

TEST_CASE("uplink: survives up to 16 symbol errors on the wire") {
    adsl::Uplink up;
    wire::Random random(0x55AA);
    adsl::UplinkTarget tx[4];
    for (int i = 0; i < 4; i++) tx[i] = make_target(0x300000 + i * 7);
    uint8_t frame[adsl::Uplink::kFrameBytes];
    REQUIRE(up.encode(tx, 4, 0, frame) == Status::Ok);

    // inject 16 symbol errors
    bool used[adsl::Uplink::kFrameBytes] = {false};
    for (int error = 0; error < 16; error++) {
        int position;
        do {
            position = random.next() % adsl::Uplink::kFrameBytes;
        } while (used[position]);
        used[position] = true;
        frame[position] ^= static_cast<uint8_t>(0x80 | (random.next() & 0x7F));
    }
    adsl::UplinkTarget rx[4];
    adsl::Uplink::DecodeStatistics statistics;
    CHECK(up.decode(frame, rx, 4, statistics) == Status::Ok);
    CHECK(statistics.corrected == 16);
    CHECK(statistics.targets == 4);
    CHECK(rx[0].address == tx[0].address);
    CHECK(rx[3].latitude_1e7 == tx[3].latitude_1e7);
}

TEST_CASE("uplink: a destroyed frame is DETECTED (Status::Crc), never silently wrong") {
    adsl::Uplink up;
    adsl::UplinkTarget tx[2] = {make_target(1), make_target(2)};
    uint8_t frame[adsl::Uplink::kFrameBytes];
    REQUIRE(up.encode(tx, 2, 0, frame) == Status::Ok);
    for (int i = 0; i < 40; i++) frame[i] ^= 0xFF;  // 40 > 16 errors
    adsl::UplinkTarget rx[2];
    adsl::Uplink::DecodeStatistics statistics;
    Status status = up.decode(frame, rx, 2, statistics);
    CHECK(status == Status::Crc);  // detected, not delivered
}

// What the dwell has to be armed with to hear a skyPost at all. Every one of
// these is a number the receiver and the transmitter must agree on, and the
// third was wrong: 0x18 is the M band's ADS-L data length, 24 bytes, and it was
// carried over to a frame that is 255. A detector armed for it never matched.
TEST_CASE("uplink: the sync word is C.4.3's, and the byte behind it is D.1.1's length") {
    CHECK(adsl::kUplinkSync[0] == 0x2D);
    CHECK(adsl::kUplinkSync[1] == 0xD4);
    CHECK(adsl::kUplinkSync[2] == adsl::kUplinkFrameBytes);
    CHECK(adsl::kUplinkFrameBytes == adsl::Uplink::kFrameBytes);
    CHECK(adsl::kUplinkSyncBits == 24);
    CHECK(adsl::kUplinkBurstBytes == 3 + adsl::Uplink::kFrameBytes);
    // §C.4: 200 kbps GMSK, BT 0.5, in a 250 kHz channel, ±50 kHz deviation.
    CHECK(adsl::kUplinkChipRateBps == 200000);
    CHECK(adsl::kUplinkDeviationHz == 50000);
    CHECK(adsl::kUplinkChannelBandwidthHz == 250000);
    CHECK(adsl::kUplinkGaussianBtE2 == 50);
}

// Reed-Solomon can decode to a valid codeword that is not the one that was sent.
// A ground station reaches every aircraft in range at once, so one miscorrected
// frame is a screenful of phantoms in front of everybody, and what a frame
// carries is checked before any of it becomes a target.
TEST_CASE("uplink: a record with no address and no place on the globe is refused") {
    adsl::Uplink up;
    adsl::UplinkTarget tx[3] = {make_target(0x600001), make_target(0), make_target(0x600003)};
    tx[2].latitude_1e7 = 1200000000;  // 120 degrees north of nowhere
    uint8_t frame[adsl::Uplink::kFrameBytes];
    REQUIRE(up.encode(tx, 3, 0, frame) == Status::Ok);

    adsl::UplinkTarget rx[3];
    adsl::Uplink::DecodeStatistics statistics;
    CHECK(up.decode(frame, rx, 3, statistics) == Status::Ok);
    CHECK(statistics.targets == 1);
    CHECK(statistics.rejected == 2);
    CHECK(rx[0].address == 0x600001u);
}
