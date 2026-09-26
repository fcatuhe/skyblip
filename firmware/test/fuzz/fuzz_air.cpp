// A burst as the radio hands it over, named and decoded in the order TrafficService does.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/events/rf.h"
#include "core/events/stamp.h"
#include "core/fec/reed_solomon.h"
#include "core/model/aircraft.h"
#include "core/model/band.h"
#include "core/protocol/adsl.h"
#include "core/protocol/adsl_uplink.h"
#include "core/protocol/air.h"
#include "core/protocol/alptas.h"
#include "core/traffic/callsigns.h"

using namespace skyblip;

namespace {

constexpr int32_t kPoleLat1e7 = 900000000;
constexpr int32_t kAntimeridianLon1e7 = 1800000000;

struct Receiver {
    uint32_t utc;
    int32_t lat_1e7;
    int32_t lon_1e7;
};

void seal_adsl(uint8_t* frame) {
    protocol::AdslPacket p{};
    p.init();
    std::memcpy(p.Data, frame, protocol::kAdslFrameBytes);
    p.set_crc();
    std::memcpy(frame, p.Data, protocol::kAdslFrameBytes);
}

// One M-band transmitter, then the chips past the shared sync window the radio consumed.
size_t transmit_mband(FuzzedDataProvider& in, bool sealed, uint8_t* chips) {
    const bool adsl = in.ConsumeBool();
    const uint32_t sync_word = adsl ? protocol::kAdslSyncWord : protocol::kAlptasSyncWord;
    const uint8_t frame_len = adsl ? protocol::kAdslFrameBytes : protocol::kAlptasFrameBytes;
    uint8_t frame[protocol::kAlptasFrameBytes]{};
    in.ConsumeData(frame, frame_len);
    if (sealed && adsl) seal_adsl(frame);
    if (sealed && !adsl) protocol::alptas_set_crc(frame);
    protocol::mband_payload(sync_word, frame, frame_len, chips);
    return protocol::kRxChipBytes;
}

size_t transmit_uplink(FuzzedDataProvider& in, bool sealed, uint8_t* codeword) {
    static const fec::ReedSolomon255 parity;
    in.ConsumeData(codeword, fec::ReedSolomon255::kK);
    if (sealed) parity.encode(codeword, codeword + fec::ReedSolomon255::kK);
    return protocol::kUplinkFrameBytes;
}

void hear_adsl(protocol::Frame& frame, const Receiver& at) {
    protocol::AdslPacket p{};
    p.init();
    std::memcpy(p.Data, frame.data, protocol::kAdslFrameBytes);
    if (p.check_crc() != 0 && (p.correct(frame.err) < 0 || p.check_crc() != 0)) return;
    p.descramble();

    if (p.is_registration()) {
        char callsign[traffic::CallsignTable::kTextBytes];
        protocol::callsign_of(p, callsign, sizeof(callsign));
        return;
    }
    events::Stamp received{};
    received.at_s = at.utc;
    model::AircraftObs obs{};
    protocol::to_obs(p, received, 0, model::Source::AdslDirect, obs);
}

void hear_alptas(protocol::Frame& frame, const Receiver& at) {
    if (protocol::alptas_correct(frame.data, frame.err) < 0) return;
    protocol::alptas_address(frame.data);
    model::AircraftObs obs{};
    protocol::alptas_decode(frame.data, at.utc, at.lat_1e7, at.lon_1e7, obs);
    uint32_t keyed = 0;
    protocol::alptas_keyed_second(frame.data, at.utc, keyed);
}

void hear_uplink(const uint8_t* codeword) {
    static const protocol::AdslUplink uplink;
    model::AircraftObs relayed[protocol::AdslUplink::kMaxTargets];
    protocol::AdslUplink::DecodeStats stats{};
    uplink.decode(codeword, relayed, protocol::AdslUplink::kMaxTargets, stats);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider in(data, size);
    Receiver at{};
    at.utc = in.ConsumeIntegral<uint32_t>();
    at.lat_1e7 = in.ConsumeIntegralInRange<int32_t>(-kPoleLat1e7, kPoleLat1e7);
    at.lon_1e7 = in.ConsumeIntegralInRange<int32_t>(-kAntimeridianLon1e7, kAntimeridianLon1e7);
    const bool sealed = in.ConsumeBool();
    const model::Band band = in.ConsumeBool() ? model::Band::O : model::Band::M;

    uint8_t burst[events::kRfEventBytes]{};
    const size_t len = band == model::Band::O ? transmit_uplink(in, sealed, burst)
                                              : transmit_mband(in, sealed, burst);
    const std::vector<uint8_t> noise = in.ConsumeRemainingBytes<uint8_t>();
    for (size_t i = 0; i < noise.size() && i < sizeof(burst); i++) burst[i] ^= noise[i];

    protocol::Frame frame{};
    switch (protocol::receive_burst(band, burst, len, frame)) {
        case protocol::System::AdslDirect: hear_adsl(frame, at); break;
        case protocol::System::Alptas: hear_alptas(frame, at); break;
        case protocol::System::AdslUplink: hear_uplink(burst); break;
        case protocol::System::Unknown: protocol::framed_noise(frame); break;
    }
    return 0;
}
