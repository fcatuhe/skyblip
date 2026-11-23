// ADS-L 4 SRD-860 issue 2 Subpart D, the data link layer: packet framing, construction order, media
// access.
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "doctest/doctest.h"
#include "protocols/adsl/media_access.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/physical.h"
#include "protocols/adsl/test/conformance/reference.h"
#include "protocols/adsl/uplink.h"
#include "protocols/common/crc.h"
#include "protocols/common/xxtea.h"

using namespace skyblip;
using namespace skyblip::adsl::conformance;

// INFO: fc 18sep26 prose: the three duties of the layer, each specified in the clauses below
TEST_CASE("ADS-L.4.SRD860.D.1: the data link mediates the channel, adds parity, may sign" *
          doctest::skip()) {
    FAIL("prose: the layer's remit, specified by D.1.1, D.1.2, D.3 and E.3");
}

// The M-band length byte rides at the end of the sync word, so the receiver reads it as it frames.
TEST_CASE(
    "ADS-L.4.SRD860.D.1.1: the Packet Length field counts the message behind it, not itself") {
    const int network_header = 1;
    const int adsl_data = 20;
    const int parity = 3;
    CHECK(int(adsl::Packet::kDataBytes) == network_header + adsl_data + parity);
    CHECK(sizeof(adsl::Packet) == adsl::Packet::kDataBytes);
    CHECK(int(adsl::kSyncWord & 0xFFu) == adsl::Packet::kDataBytes);
    const int uplink_header = 5;
    const int fec_parity = 32;
    CHECK(adsl::uplink_length(1) == network_header + uplink_header + 19 + fec_parity);
    CHECK(adsl::uplink_length(10) == network_header + uplink_header + 191 + fec_parity);
}

// G.2's 56 bits behind the 40-bit header are 12 bytes of data: with the header byte and the
// CRC, 16.
TEST_CASE("ADS-L.4.SRD860.D.1.1: a Status goes out as 16 bytes, and its length byte says so") {
    adsl::Packet status{};
    adsl::encode_status(status, 0x123456, adsl::kAddressTableFlarm, adsl::StatusReport{});
    CHECK(status.data_bytes() == 1 + 12 + 3);
    CHECK(int(adsl::sync_word(status.data_bytes()) & 0xFFu) == 16);
    CHECK((adsl::sync_word(status.data_bytes()) >> 8) == (adsl::kSyncWord >> 8));
    CHECK(traffic_packet().data_bytes() == adsl::Packet::kDataBytes);
    CHECK(adsl::sync_word(adsl::Packet::kDataBytes) == adsl::kSyncWord);
}

// "Some payload types have variable lengths, in which case the receiver must consult this field."
TEST_CASE("ADS-L.4.SRD860.D.1.1: a receiver reads a 16-byte Status built the way the clauses say" *
          doctest::test_suite("main")) {
    adsl::Packet clear{};
    adsl::encode_status(clear, 0x123456, adsl::kAddressTableFlarm, adsl::StatusReport{});
    uint8_t frame[16] = {0};
    std::memcpy(frame, clear.data, 13);
    uint32_t words[3];
    std::memcpy(words, frame + 1, sizeof(words));
    wire::xxtea_encrypt(words, 3, wire::kXxteaZeroKey, 6);
    std::memcpy(frame + 1, words, sizeof(words));
    const uint32_t crc = wire::crc24_compute(frame, 13);
    frame[13] = static_cast<uint8_t>(crc >> 16);
    frame[14] = static_cast<uint8_t>(crc >> 8);
    frame[15] = static_cast<uint8_t>(crc);

    const uint8_t erasures[16] = {0};
    adsl::Packet heard;
    REQUIRE(adsl::receive(frame, erasures, heard, 16) == wire::Status::Ok);
    CHECK(heard.payload_type == adsl::Packet::kPayloadTypeStatus);
    CHECK(heard.address() == 0x123456u);
    CHECK(std::memcmp(heard.data, clear.data, 13) == 0);
    for (int i = 13; i < adsl::Packet::kDataBytes; i++) CHECK(heard.data[i] == 0);

    uint8_t sent[16];
    adsl::Packet ours = clear;
    ours.scramble(16);
    ours.set_crc(16);
    std::memcpy(sent, ours.data, 16);
    CHECK(std::memcmp(sent, frame, 16) == 0);
    CHECK(adsl::receive(frame, erasures, heard, 20) == wire::Status::Invalid);
}

TEST_CASE("ADS-L.4.SRD860.D.1.1: a doubtful length can be recovered at the size the CRC checks") {
    uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    adsl::Packet heard;

    adsl::Packet status{};
    adsl::encode_status(status, 0x000008, adsl::kAddressTableFlarm, adsl::StatusReport{});
    status.scramble(adsl::Packet::kStatusDataBytes);
    status.set_crc(adsl::Packet::kStatusDataBytes);
    uint8_t short_burst[adsl::Packet::kDataBytes] = {0};
    std::memcpy(short_burst, status.data, adsl::Packet::kStatusDataBytes);
    // Past a 16-byte burst the receiver hears no transmitter, so every chip pair there is marked.
    for (int i = adsl::Packet::kStatusDataBytes; i < adsl::Packet::kDataBytes; i++)
        erasures[i] = 0xFF;
    for (const uint8_t length : {uint8_t{16}, uint8_t{24}}) {
        CAPTURE(int(length));
        REQUIRE(adsl::receive_as_heard(short_burst, erasures, length, true, heard) ==
                wire::Status::Ok);
        CHECK(heard.payload_type == adsl::Packet::kPayloadTypeStatus);
        CHECK(heard.address() == 0x000008u);
    }

    std::memset(erasures, 0, sizeof(erasures));
    adsl::Packet traffic = traffic_packet();
    traffic.scramble();
    traffic.set_crc();
    for (const uint8_t length : {uint8_t{24}, uint8_t{16}}) {
        CAPTURE(int(length));
        REQUIRE(adsl::receive_as_heard(traffic.data, erasures, length, true, heard) ==
                wire::Status::Ok);
        CHECK(heard.is_position());
        CHECK(heard.address() == 0x123456u);
    }
}

// A repair that fits the wrong length must not hide the right one, found by a review probe.
TEST_CASE("ADS-L.4.SRD860.D.1.1: doubtful length Traffic is recovered even where 16 bytes repair") {
    const uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    adsl::Packet traffic = traffic_packet(0x00094D);
    traffic.scramble();
    traffic.set_crc();
    adsl::Packet heard;
    REQUIRE(adsl::receive_as_heard(traffic.data, erasures, 16, true, heard) == wire::Status::Ok);
    CHECK(heard.is_position());
    CHECK(heard.address() == 0x00094Du);
}

// A clean CRC codeword stays one with zeros behind it, so a Status before 0x00 chips reads both
// ways.
TEST_CASE("ADS-L.4.SRD860.D.1.1: a doubtful length that checks at both sizes is refused") {
    const uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    adsl::Packet status{};
    adsl::encode_status(status, 0x000021, adsl::kAddressTableFlarm, adsl::StatusReport{});
    status.scramble(adsl::Packet::kStatusDataBytes);
    status.set_crc(adsl::Packet::kStatusDataBytes);
    uint8_t frame[adsl::Packet::kDataBytes] = {0};
    std::memcpy(frame, status.data, adsl::Packet::kStatusDataBytes);
    adsl::Packet heard;
    for (const uint8_t length : {uint8_t{24}, uint8_t{16}}) {
        CAPTURE(int(length));
        CHECK(adsl::receive_as_heard(frame, erasures, length, true, heard) ==
              wire::Status::Invalid);
        CHECK(heard.address() == 0u);
    }
    REQUIRE(adsl::receive_as_heard(frame, erasures, 16, false, heard) == wire::Status::Ok);
    CHECK(heard.payload_type == adsl::Packet::kPayloadTypeStatus);
}

// Each vector has a valid CRC at the tested size, but its decoded payload requires another size.
TEST_CASE("ADS-L.4.SRD860.D.1.1: valid parity does not allow a padded Status or cut Traffic") {
    const uint8_t erasures[adsl::Packet::kDataBytes] = {0};
    adsl::Packet heard;

    adsl::Packet padded{};
    adsl::encode_status(padded, 0x123456, adsl::kAddressTableFlarm, adsl::StatusReport{});
    padded.scramble(adsl::Packet::kDataBytes);
    padded.set_crc(adsl::Packet::kDataBytes);
    CHECK(adsl::receive(padded.data, erasures, heard) == wire::Status::Invalid);

    adsl::Packet cut = traffic_packet();
    cut.scramble(adsl::Packet::kStatusDataBytes);
    cut.set_crc(adsl::Packet::kStatusDataBytes);
    CHECK(adsl::receive(cut.data, erasures, heard, adsl::Packet::kStatusDataBytes) ==
          wire::Status::Invalid);
}

// INFO: fc 13nov26 optional: G.2.3 lets a Status declare O-band LDR receive None, as ours does
TEST_CASE(
    "ADS-L.4.SRD860.D.1.2: the O-band LDR packet is a 6-byte header, 24 bytes of data and a "
    "CRC8" *
    doctest::skip()) {
    FAIL("optional: O-band LDR is not taken, so no header, no CRC8 and no 38.4 kbps dwell");
}

// The parity covers the scrambled form, so a receiver checks it before it can read a single field.
TEST_CASE("ADS-L.4.SRD860.D.2: data, then scrambling, then parity over the network header too") {
    adsl::Packet packet = traffic_packet();
    const adsl::Packet plain = packet;
    packet.scramble();
    packet.set_crc();
    CHECK(std::memcmp(packet.payload, plain.payload, 20) != 0);
    CHECK(packet.crc_ok());

    adsl::Packet flipped = packet;
    flipped.version ^= 0x01;
    CHECK_FALSE(flipped.crc_ok());

    adsl::Packet descrambled = packet;
    descrambled.descramble();
    CHECK_FALSE(descrambled.crc_ok());
    CHECK(std::memcmp(descrambled.payload, plain.payload, 20) == 0);
}

// The clause's two numbers, each read from the side a device could get wrong.
TEST_CASE("ADS-L.4.SRD860.D.3: a busy channel defers, forces after 3000 ms, then 2000 ms quiet") {
    adsl::ChannelAttempt attempt{};
    attempt.at_us = 20000000;
    attempt.first_us = attempt.at_us;
    attempt.earliest_us = attempt.at_us;
    attempt.last_us = attempt.at_us + 300000;
    attempt.may_force = true;
    const adsl::Deferral deferral{5000, 8, 3};

    attempt.clear = true;
    CHECK(adsl::media_access(attempt, deferral).access == adsl::Access::Transmit);
    attempt.clear = false;
    const adsl::AccessVerdict deferred = adsl::media_access(attempt, deferral);
    CHECK(deferred.access == adsl::Access::Defer);
    CHECK(deferred.at_us > attempt.at_us);

    attempt.first_us = attempt.at_us - 2999000;
    CHECK(adsl::media_access(attempt, deferral).access == adsl::Access::Defer);
    attempt.first_us = attempt.at_us - 3000000;
    const adsl::AccessVerdict forced = adsl::media_access(attempt, deferral);
    CHECK(forced.access == adsl::Access::Force);

    adsl::ChannelAttempt after = attempt;
    after.clear = true;
    after.forced_end_us = forced.at_us + 5000;
    after.at_us = forced.at_us + 5000 + 1999000;
    CHECK(adsl::media_access(after, deferral).access == adsl::Access::Silent);
    after.at_us = forced.at_us + 5000 + 2000000;
    CHECK(adsl::media_access(after, deferral).access == adsl::Access::Transmit);
}
