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
constexpr uint32_t kStation = 0x00FFFE;
constexpr uint8_t kStationTable = 9;

adsl::Packet make_target(uint32_t address, uint8_t timestamp = 0) {
    adsl::Traffic traffic;
    traffic.address = address & 0xFFFFFF;
    traffic.timestamp = timestamp;
    traffic.aircraft_category = 4;
    traffic.flight_state = 2;
    traffic.emergency = adsl::kEmergencyNone;
    traffic.position_valid = true;
    traffic.latitude_1e7 = 481234500;
    traffic.longitude_1e7 = 81234500;
    traffic.altitude_valid = true;
    traffic.altitude_m = 1234;
    traffic.speed_valid = true;
    traffic.speed_q = 180;
    traffic.climb_valid = true;
    traffic.climb_e8 = -12;
    traffic.track_c9 = 77;
    adsl::Packet packet{};
    adsl::encode_traffic(packet, adsl::kAddressTableFlarm, traffic);
    return packet;
}

int sent(const adsl::Packet* targets, int count, uint8_t* packet) {
    adsl::Uplink up;
    REQUIRE(up.encode(kStation, kStationTable, targets, count, packet) == Status::Ok);
    return 1 + packet[0];
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
    adsl::Packet tx[adsl::kUplinkMaxTargets];
    for (int i = 0; i < adsl::kUplinkMaxTargets; i++) tx[i] = make_target(0x200000 + i);
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    CHECK(sent(tx, adsl::kUplinkMaxTargets, packet) == 1 + adsl::kUplinkMaxLength);

    adsl::Uplink up;
    adsl::Packet rx[adsl::kUplinkMaxTargets];
    adsl::UplinkStatistics statistics;
    CHECK(up.decode(packet, sizeof(packet), rx, adsl::kUplinkMaxTargets, statistics) == Status::Ok);
    CHECK(statistics.targets == adsl::kUplinkMaxTargets);
    CHECK(statistics.corrected == 0);
    for (int i = 0; i < adsl::kUplinkMaxTargets; i++)
        CHECK(std::memcmp(rx[i].payload + 1, tx[i].payload + 1, adsl::Packet::kPayloadBytes - 1) ==
              0);
}

TEST_CASE("uplink: survives up to 16 symbol errors on the wire") {
    wire::Random random(0x55AA);
    adsl::Packet tx[4];
    for (int i = 0; i < 4; i++) tx[i] = make_target(0x300000 + i * 7);
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    const int bytes = sent(tx, 4, packet);

    bool used[1 + adsl::kUplinkMaxLength] = {false};
    for (int error = 0; error < 16; error++) {
        int position;
        do {
            position = 1 + static_cast<int>(random.next() % static_cast<uint32_t>(bytes - 1));
        } while (used[position]);
        used[position] = true;
        packet[position] ^= static_cast<uint8_t>(0x80 | (random.next() & 0x7F));
    }
    adsl::Uplink up;
    adsl::Packet rx[4];
    adsl::UplinkStatistics statistics;
    CHECK(up.decode(packet, static_cast<size_t>(bytes), rx, 4, statistics) == Status::Ok);
    CHECK(statistics.corrected == 16);
    CHECK(statistics.targets == 4);
    CHECK(rx[0].address() == tx[0].address());
    CHECK(rx[3].latitude_1e7() == tx[3].latitude_1e7());
}

TEST_CASE("uplink: a destroyed frame is DETECTED (Status::Crc), never silently wrong") {
    adsl::Packet tx[2] = {make_target(1), make_target(2)};
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent(tx, 2, packet);
    for (int i = 1; i < 41; i++) packet[i] ^= 0xFF;
    adsl::Uplink up;
    adsl::Packet rx[2];
    adsl::UplinkStatistics statistics;
    CHECK(up.decode(packet, sizeof(packet), rx, 2, statistics) == Status::Crc);
}

// What the dwell has to be armed with to hear a skyPost at all: C.4.3's two bytes, then the length.
TEST_CASE("uplink: the sync word is C.4.3's, and the byte behind it is D.1.1's length") {
    CHECK(adsl::kUplinkSync[0] == 0x2D);
    CHECK(adsl::kUplinkSync[1] == 0xD4);
    CHECK(adsl::kUplinkSyncBits == 16);
    adsl::Packet tx[3] = {make_target(1), make_target(2), make_target(3)};
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent(tx, 3, packet);
    uint8_t burst[adsl::kUplinkMaxBurstBytes];
    CHECK(adsl::encode_o_band(packet, burst) == 3u + adsl::uplink_length(3));
    CHECK(burst[2] == adsl::uplink_length(3));
    // §C.4: 200 kbps GMSK, BT 0.5, in a 250 kHz channel, ±50 kHz deviation.
    CHECK(adsl::kUplinkChipRateBps == 200000);
    CHECK(adsl::kUplinkDeviationHz == 50000);
    CHECK(adsl::kUplinkChannelBandwidthHz == 250000);
    CHECK(adsl::kUplinkGaussianBtE2 == 50);
}

// A length that names no whole number of records is not an uplink packet, whatever it holds.
TEST_CASE("uplink: a length G.3 cannot spell is refused before any decoding") {
    adsl::Packet tx[1] = {make_target(1)};
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent(tx, 1, packet);
    adsl::Uplink up;
    adsl::Packet rx[1];
    adsl::UplinkStatistics statistics;
    packet[0] = static_cast<uint8_t>(packet[0] + 1);
    CHECK(up.decode(packet, sizeof(packet), rx, 1, statistics) == Status::Invalid);
    packet[0] = static_cast<uint8_t>(packet[0] - 1);
    CHECK(up.decode(packet, 10, rx, 1, statistics) == Status::Invalid);
}

// Reed-Solomon can decode to a valid codeword that is not the one that was sent.
// A ground station reaches every aircraft in range at once, so one miscorrected
// frame is a screenful of phantoms in front of everybody, and what a frame
// carries is checked before any of it becomes a target.
TEST_CASE("uplink: a record with no address and no place on the globe is refused") {
    adsl::Packet tx[3] = {make_target(0x600001), make_target(0), make_target(0x600003)};
    tx[2].set_position_invalid();
    uint8_t packet[1 + adsl::kUplinkMaxLength];
    sent(tx, 3, packet);

    adsl::Uplink up;
    adsl::Packet rx[3];
    adsl::UplinkStatistics statistics;
    CHECK(up.decode(packet, sizeof(packet), rx, 3, statistics) == Status::Ok);
    CHECK(statistics.targets == 1);
    CHECK(statistics.rejected == 2);
    CHECK(rx[0].address() == 0x600001u);
}

// G.1.1's timestamp counts quarters inside 15 s, so 10 s of age is 40 quarters behind.
TEST_CASE("uplink: a relayed record is current up to 10 s old, and never from the future") {
    const uint32_t utc_s = 1785628815;  // 0 quarters into the 15 s cycle
    const adsl::Packet fresh = make_target(1, 0);
    CHECK(adsl::uplink_record_current(fresh, utc_s, 0));
    CHECK(adsl::uplink_record_current(fresh, utc_s + 10, 0));
    CHECK_FALSE(adsl::uplink_record_current(fresh, utc_s + 10, 250));
    const adsl::Packet ahead = make_target(1, 1);
    CHECK_FALSE(adsl::uplink_record_current(ahead, utc_s, 0));
    CHECK(adsl::uplink_record_current(ahead, utc_s, 250));
}
