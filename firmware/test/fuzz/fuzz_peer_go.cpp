// Anything named like a Bravo notifying a Go on the peer endpoint, while it pairs or once paired.
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/comms/peer.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 25sep26 the panel's modelled boot refresh is 95% of setup, and no frame needs a panel
constexpr ports::Capabilities kNoGlass =
    static_cast<ports::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                     ~static_cast<uint32_t>(ports::Capability::Display));

constexpr events::BluetoothAddress kBravoAddress{1, {0x2A, 0x3F, 0x11, 0x22, 0x33, 0xC4}};
constexpr const char* kBravoName = "skyBlip Bravo 3F2A";
constexpr uint8_t kPairedFlag = 0x01;
constexpr uint32_t kPassGapMs = 100;
constexpr uint32_t kSecondMs = 1000;
constexpr int kMaxActions = 16;
constexpr size_t kNotifyHeadBytes = 3;
constexpr int32_t kTaxiMmS = 0;
constexpr int32_t kFieldAltitudeM = 300;

void pass_gap(Rig& rig, uint32_t& now_ms) {
    rig.run(now_ms, now_ms + kPassGapMs);
    now_ms += kPassGapMs;
}

// The central comes up on whatever the Go asked to connect to, as a Bravo in range answers.
void answer_connect(Rig& rig, uint32_t& now_ms) {
    pass_gap(rig, now_ms);
    rig.platform.central().come_up(comms::kPeerPayloadFloorBytes);
    pass_gap(rig, now_ms);
}

// The BRAVO page's walk without the glass: scan, hear the one Bravo, pick it, connect.
void start_pairing(Rig& rig, uint32_t& now_ms) {
    go::BravoService& service = rig.product.bravo();
    service.begin_pairing(now_ms);
    pass_gap(rig, now_ms);
    rig.platform.central().hear(kBravoAddress, kBravoName);
    pass_gap(rig, now_ms);
    service.pair(0, now_ms);
    answer_connect(rig, now_ms);
    if (service.step() != go::BravoStep::Waiting) __builtin_trap();
}

void start_paired(Rig& rig, uint32_t& now_ms) {
    rig.settings().bravo = kBravoAddress;
    answer_connect(rig, now_ms);
    if (!rig.platform.central().up()) __builtin_trap();
}

// INFO: fc 21nov26 a longer notification is skipped by hardware/platform/zephyr/central.cpp
void notify(Rig& rig, const uint8_t* bytes, size_t byte_count) {
    if (byte_count > sizeof(events::RxFrame::data)) return;
    (void)rig.platform.central().notify(
        std::string(reinterpret_cast<const char*>(bytes), byte_count));
}

void reconnect(Rig& rig, uint32_t& now_ms) {
    rig.platform.central().drop();
    answer_connect(rig, now_ms);
}

size_t act(Rig& rig, uint32_t& now_ms, const uint8_t* input, size_t left) {
    switch (input[0]) {
        case 'S': rig.second(now_ms, kTaxiMmS, kFieldAltitudeM); return 1;
        case 'W':
            rig.run(now_ms, now_ms + kSecondMs);
            now_ms += kSecondMs;
            return 1;
        case 'D': reconnect(rig, now_ms); return 1;
        case 'U': rig.product.bravo().unpair(); return 1;
        case 'N': break;
        default: return 1;
    }
    if (left < kNotifyHeadBytes) return left;
    const size_t declared = static_cast<size_t>(input[1] | (input[2] << 8));
    const size_t frame_bytes =
        declared < left - kNotifyHeadBytes ? declared : left - kNotifyHeadBytes;
    notify(rig, input + kNotifyHeadBytes, frame_bytes);
    pass_gap(rig, now_ms);
    return kNotifyHeadBytes + frame_bytes;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    Rig rig(kNoGlass);
    if (rig.setup() != Status::Ok) __builtin_trap();
    uint32_t now_ms = 0;
    rig.second(now_ms, kTaxiMmS, kFieldAltitudeM);
    if (data[0] & kPairedFlag)
        start_paired(rig, now_ms);
    else
        start_pairing(rig, now_ms);

    size_t at = 1;
    for (int action = 0; action < kMaxActions && at < size; action++)
        at += act(rig, now_ms, data + at, size - at);
    return 0;
}
