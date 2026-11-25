// A burst as the radio hands it over, named and decoded in the order TrafficService does.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/events/rf.h"
#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/protocol/alptas.h"
#include "core/protocol/callsign.h"
#include "core/traffic/callsigns.h"
#include "protocols/adsl/packet.h"
#include "protocols/adsl/reed_solomon.h"
#include "protocols/adsl/uplink.h"
#include "protocols/adsl_edge/registration.h"
#include "protocols/common/crc.h"
#include "simulator/world/alptas_out.h"

using namespace skyblip;

namespace {

constexpr int32_t kPoleLatitude1e7 = 900000000;
constexpr int32_t kAntimeridianLongitude1e7 = 1800000000;

struct Receiver {
    uint32_t utc_s;
    int32_t latitude_1e7;
    int32_t longitude_1e7;
};

// Raw sends the input's bytes, Sealed fixes their checksum, Encoded builds the frame from fields.
enum class Sealing : uint8_t { Raw, Sealed, Encoded };

void seal_adsl(uint8_t* frame, uint8_t bytes, bool build_status) {
    adsl::Packet packet{};
    std::memcpy(packet.data, frame, bytes);
    if (build_status) {
        packet.version = adsl::Packet::kProtocolVersionIssue2;
        packet.payload_type = adsl::Packet::kPayloadTypeStatus;
        packet.scramble(bytes);
    }
    packet.set_crc(bytes);
    std::memcpy(frame, packet.data, bytes);
}

// ALP-TAS is encrypted under its second, so only an encoder gets a mutation past the key.
void encode_alptas(FuzzedDataProvider& in, const Receiver& receiver, uint8_t* frame) {
    model::AircraftObservation observation{};
    observation.address = in.ConsumeIntegral<uint32_t>();
    observation.address_mapping_table = in.ConsumeIntegral<uint8_t>();
    observation.aircraft_category = in.ConsumeIntegral<uint8_t>();
    observation.flight_state = in.ConsumeIntegral<uint8_t>();
    observation.latitude_1e7 =
        in.ConsumeIntegralInRange<int32_t>(-kPoleLatitude1e7, kPoleLatitude1e7);
    observation.longitude_1e7 =
        in.ConsumeIntegralInRange<int32_t>(-kAntimeridianLongitude1e7, kAntimeridianLongitude1e7);
    observation.altitude_m = in.ConsumeIntegral<int16_t>();
    observation.climb_e8 = in.ConsumeIntegral<int16_t>();
    observation.speed_q = in.ConsumeIntegral<uint16_t>();
    observation.track_c9 = in.ConsumeIntegral<uint16_t>();
    observation.climb_valid = in.ConsumeBool();
    observation.speed_valid = in.ConsumeBool();
    observation.position_valid = true;
    const int32_t keyed_off_s =
        in.ConsumeIntegralInRange<int32_t>(-alptas::kKeyWindowS, alptas::kKeyWindowS);
    (void)simulator::alptas_encode(frame, observation,
                                   receiver.utc_s + static_cast<uint32_t>(keyed_off_s));
}

// One M-band transmitter, then the chips past the shared sync window the radio consumed.
size_t transmit_m_band(FuzzedDataProvider& in, Sealing sealing, const Receiver& receiver,
                       uint8_t* chips) {
    uint8_t frame[alptas::kFrameBytes]{};
    if (in.ConsumeBool()) {
        const bool short_frame = in.ConsumeBool();
        const uint8_t bytes = short_frame ? adsl::Packet::kStatusDataBytes : adsl::kFrameBytes;
        const uint8_t length =
            in.PickValueInArray({bytes, adsl::Packet::kStatusDataBytes, adsl::Packet::kDataBytes,
                                 in.ConsumeIntegral<uint8_t>()});
        const bool erase_length_chip = in.ConsumeBool();
        const unsigned bit = in.ConsumeIntegralInRange<unsigned>(0, 7);
        in.ConsumeData(frame, bytes);
        if (sealing != Sealing::Raw)
            seal_adsl(frame, bytes, short_frame && sealing == Sealing::Encoded);
        protocol::m_band_payload(adsl::sync_word(length), frame, bytes, chips);
        if (erase_length_chip) {
            uint8_t* length_chips = chips + protocol::kSyncTailChipBytes - 2;
            length_chips[1 - bit / 4] ^= static_cast<uint8_t>(2u << (2 * (bit % 4)));
        }
    } else {
        if (sealing == Sealing::Encoded)
            encode_alptas(in, receiver, frame);
        else
            in.ConsumeData(frame, alptas::kFrameBytes);
        if (sealing == Sealing::Sealed) alptas::set_crc(frame);
        protocol::m_band_payload(alptas::kSyncWord, frame, alptas::kFrameBytes, chips);
    }
    return protocol::kRxChipBytes;
}

// One parity over a G.3 packet: E.3.1's CRC-24, most significant byte first, or E.3.2's code.
void seal_uplink(uint8_t* packet, int length, adsl::ErrorControl mode) {
    const int covered = length - adsl::uplink_parity_bytes(mode);
    if (mode == adsl::ErrorControl::Crc) {
        const uint32_t crc = wire::crc24_compute(packet + 1, static_cast<size_t>(covered));
        packet[1 + covered] = static_cast<uint8_t>(crc >> 16);
        packet[2 + covered] = static_cast<uint8_t>(crc >> 8);
        packet[3 + covered] = static_cast<uint8_t>(crc);
        return;
    }
    static const adsl::ReedSolomon255 parity;
    uint8_t block[adsl::ReedSolomon255::kDataBytes]{};
    std::memcpy(block + adsl::ReedSolomon255::kDataBytes - covered, packet + 1,
                static_cast<size_t>(covered));
    parity.encode(block, packet + 1 + covered);
}

// A G.3 packet behind its D.1.1 length: raw, sealed by either parity, or encoded by adsl::Uplink.
size_t transmit_uplink(FuzzedDataProvider& in, Sealing sealing, const Receiver& receiver,
                       uint8_t* packet) {
    const int count = in.ConsumeIntegralInRange<int>(0, adsl::kUplinkMaxTargets);
    if (sealing == Sealing::Encoded) {
        adsl::Packet records[adsl::kUplinkMaxTargets];
        for (int i = 0; i < count; i++) {
            records[i].reset(adsl::Packet::kPayloadTypePosition);
            in.ConsumeData(records[i].payload + 1, adsl::Packet::kPayloadBytes - 1);
            const int32_t age_quarters = in.ConsumeIntegralInRange<int32_t>(-8, 60);
            records[i].timestamp = adsl::timestamp_code(receiver.utc_s, -250 * age_quarters);
        }
        static const adsl::Uplink uplink;
        (void)uplink.encode(in.ConsumeIntegral<uint32_t>() & 0xFFFFFFu,
                            in.ConsumeIntegralInRange<uint8_t>(0, 63), records, count, packet);
        return 1u + static_cast<size_t>(packet[0]);
    }
    const adsl::ErrorControl mode =
        in.ConsumeBool() ? adsl::ErrorControl::Fec : adsl::ErrorControl::Crc;
    const int length = adsl::uplink_length(count, mode);
    packet[0] = in.ConsumeBool() ? static_cast<uint8_t>(length) : in.ConsumeIntegral<uint8_t>();
    in.ConsumeData(packet + 1, adsl::kUplinkMaxLength);
    if (sealing == Sealing::Sealed) seal_uplink(packet, length, mode);
    return 1u + adsl::kUplinkMaxLength;
}

void hear_adsl(protocol::Frame& frame, const Receiver& receiver) {
    adsl::Packet packet;
    if (adsl::receive_as_heard(frame.data, frame.erasures, frame.length_heard,
                               frame.length_erasures, packet) != wire::Status::Ok)
        return;

    if (adsl_edge::is_registration(packet)) {
        char callsign[traffic::CallsignTable::kTextBytes];
        protocol::callsign_of(packet, callsign, sizeof(callsign));
        return;
    }
    events::Stamp received{};
    received.at_s = receiver.utc_s;
    model::AircraftObservation observation{};
    protocol::to_observation(packet, received, 0, model::Source::AdslDirect, observation);
}

void hear_alptas(protocol::Frame& frame, const Receiver& receiver) {
    if (alptas::receive(frame.data, frame.erasures) != wire::Status::Ok) return;
    alptas::address(frame.data);
    model::AircraftObservation observation{};
    if (protocol::alptas_decode(frame.data, receiver.utc_s, receiver.latitude_1e7,
                                receiver.longitude_1e7, observation) == Status::Ok)
        return;
    uint32_t keyed = 0;
    (void)alptas::keyed_second(frame.data, receiver.utc_s, keyed);
}

void hear_uplink(const uint8_t* packet, size_t bytes, const Receiver& receiver, int32_t into_ms) {
    static const protocol::AdslUplink uplink;
    model::AircraftObservation relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStatistics statistics{};
    (void)uplink.decode(packet, bytes, receiver.utc_s, into_ms, relayed,
                        protocol::AdslUplink::kMaxTargets, statistics);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider in(data, size);
    Receiver receiver{};
    receiver.utc_s = in.ConsumeIntegral<uint32_t>();
    receiver.latitude_1e7 = in.ConsumeIntegralInRange<int32_t>(-kPoleLatitude1e7, kPoleLatitude1e7);
    receiver.longitude_1e7 =
        in.ConsumeIntegralInRange<int32_t>(-kAntimeridianLongitude1e7, kAntimeridianLongitude1e7);
    const Sealing sealing = in.PickValueInArray({Sealing::Raw, Sealing::Sealed, Sealing::Encoded});
    const model::Band band = in.ConsumeBool() ? model::Band::O : model::Band::M;

    uint8_t burst[events::kRfEventBytes]{};
    const int32_t into_ms = in.ConsumeIntegralInRange<int32_t>(0, 999);
    const size_t bytes = band == model::Band::O ? transmit_uplink(in, sealing, receiver, burst)
                                                : transmit_m_band(in, sealing, receiver, burst);
    const std::vector<uint8_t> noise = in.ConsumeRemainingBytes<uint8_t>();
    for (size_t i = 0; i < noise.size() && i < sizeof(burst); i++) burst[i] ^= noise[i];

    protocol::Frame frame{};
    switch (protocol::receive_burst(band, burst, bytes, frame)) {
        case protocol::System::AdslDirect: hear_adsl(frame, receiver); break;
        case protocol::System::Alptas: hear_alptas(frame, receiver); break;
        case protocol::System::AdslUplink: hear_uplink(burst, bytes, receiver, into_ms); break;
        case protocol::System::Unknown: protocol::framed_noise(frame); break;
    }
    return 0;
}
