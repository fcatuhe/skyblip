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
#include "protocols/adsl/reed_solomon.h"

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

void seal_adsl(uint8_t* frame) {
    adsl::Packet packet{};
    packet.reset();
    std::memcpy(packet.data, frame, adsl::kFrameBytes);
    packet.set_crc();
    std::memcpy(frame, packet.data, adsl::kFrameBytes);
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
        in.ConsumeIntegralInRange<int32_t>(-alptas::kAlptasKeyWindowS, alptas::kAlptasKeyWindowS);
    (void)protocol::alptas_encode(frame, observation,
                                  receiver.utc_s + static_cast<uint32_t>(keyed_off_s));
}

// One M-band transmitter, then the chips past the shared sync window the radio consumed.
size_t transmit_m_band(FuzzedDataProvider& in, Sealing sealing, const Receiver& receiver,
                       uint8_t* chips) {
    const bool adsl = in.ConsumeBool();
    const uint32_t sync_word = adsl ? adsl::kSyncWord : alptas::kSyncWord;
    const uint8_t frame_bytes = adsl ? adsl::kFrameBytes : alptas::kFrameBytes;
    uint8_t frame[alptas::kFrameBytes]{};
    if (sealing == Sealing::Encoded && !adsl)
        encode_alptas(in, receiver, frame);
    else
        in.ConsumeData(frame, frame_bytes);
    if (sealing != Sealing::Raw && adsl) seal_adsl(frame);
    if (sealing == Sealing::Sealed && !adsl) alptas::set_crc(frame);
    protocol::m_band_payload(sync_word, frame, frame_bytes, chips);
    return protocol::kRxChipBytes;
}

size_t transmit_uplink(FuzzedDataProvider& in, Sealing sealing, uint8_t* codeword) {
    static const adsl::ReedSolomon255 parity;
    in.ConsumeData(codeword, adsl::ReedSolomon255::kDataBytes);
    if (sealing != Sealing::Raw)
        parity.encode(codeword, codeword + adsl::ReedSolomon255::kDataBytes);
    return adsl::kUplinkFrameBytes;
}

void hear_adsl(protocol::Frame& frame, const Receiver& receiver) {
    adsl::Packet packet{};
    packet.reset();
    std::memcpy(packet.data, frame.data, adsl::kFrameBytes);
    if (packet.check_crc() != 0 && (packet.correct(frame.erasures) < 0 || packet.check_crc() != 0))
        return;
    packet.descramble();

    if (protocol::is_registration(packet)) {
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
    if (alptas::correct(frame.data, frame.erasures) < 0) return;
    alptas::address(frame.data);
    model::AircraftObservation observation{};
    if (protocol::alptas_decode(frame.data, receiver.utc_s, receiver.latitude_1e7,
                                receiver.longitude_1e7, observation) == Status::Ok)
        return;
    uint32_t keyed = 0;
    (void)alptas::keyed_second(frame.data, receiver.utc_s, keyed);
}

void hear_uplink(const uint8_t* codeword) {
    static const protocol::AdslUplink uplink;
    model::AircraftObservation relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStatistics statistics{};
    (void)uplink.decode(codeword, relayed, protocol::AdslUplink::kMaxTargets, statistics);
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
    const size_t bytes = band == model::Band::O ? transmit_uplink(in, sealing, burst)
                                                : transmit_m_band(in, sealing, receiver, burst);
    const std::vector<uint8_t> noise = in.ConsumeRemainingBytes<uint8_t>();
    for (size_t i = 0; i < noise.size() && i < sizeof(burst); i++) burst[i] ^= noise[i];

    protocol::Frame frame{};
    switch (protocol::receive_burst(band, burst, bytes, frame)) {
        case protocol::System::AdslDirect: hear_adsl(frame, receiver); break;
        case protocol::System::Alptas: hear_alptas(frame, receiver); break;
        case protocol::System::AdslUplink: hear_uplink(burst); break;
        case protocol::System::Unknown: protocol::framed_noise(frame); break;
    }
    return 0;
}
