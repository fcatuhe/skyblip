// Two centrals writing to Bravo's peer endpoint, its pinned Go and a stranger, into the product.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/events/link.h"
#include "test/support/bravo_rig.h"

using namespace skyblip;

namespace {

constexpr uint16_t kPinnedGo = 1;
constexpr uint16_t kStranger = 2;
constexpr events::BluetoothAddress kPinnedAddress{1, {0x5B, 0x1C, 0x44, 0x55, 0x66, 0xD7}};
constexpr events::BluetoothAddress kStrangerAddress{1, {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}};
constexpr uint8_t kPinnedFlag = 0x01;
constexpr uint32_t kAircraft = 0x3C6586;
constexpr double kAircraftNorthM = 1000;
constexpr uint32_t kWriteGapMs = 100;
constexpr int kMaxActions = 16;
constexpr size_t kWriteHeadBytes = 3;

// INFO: fc 21nov26 a longer write is refused by the ATT layer (hardware/platform/zephyr/link.cpp)
void write(BravoRig& rig, uint16_t session_id, const uint8_t* bytes, size_t byte_count) {
    events::RxFrame frame{};
    if (byte_count > frame.data.size()) return;
    frame.session_id = session_id;
    frame.endpoint = events::Endpoint::Peer;
    frame.bytes = static_cast<uint16_t>(byte_count);
    std::memcpy(frame.data.data(), bytes, byte_count);
    (void)rig.platform.link().push_rx(frame);
}

// A new connection from the same Go, as a reboot or a lost link brings.
void reconnect(BravoRig& rig) {
    rig.platform.link().drop_link(kPinnedGo);
    rig.run_for(kWriteGapMs);
    rig.platform.link().raise_link(kPinnedGo, kPinnedAddress);
}

size_t act(BravoRig& rig, const uint8_t* input, size_t left) {
    switch (input[0]) {
        case 'P': rig.press(); return 1;
        case 'S': rig.seconds_with_fix(1); return 1;
        case 'A': rig.hear_aircraft(kAircraft, kAircraftNorthM); return 1;
        case 'D': reconnect(rig); return 1;
        case 'G':
        case 'g': break;
        default: return 1;
    }
    if (left < kWriteHeadBytes) return left;
    const size_t declared = static_cast<size_t>(input[1] | (input[2] << 8));
    const size_t frame_bytes =
        declared < left - kWriteHeadBytes ? declared : left - kWriteHeadBytes;
    write(rig, input[0] == 'G' ? kPinnedGo : kStranger, input + kWriteHeadBytes, frame_bytes);
    rig.run_for(kWriteGapMs);
    return kWriteHeadBytes + frame_bytes;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    BravoRig rig;
    if (rig.setup() != Status::Ok) __builtin_trap();
    if (data[0] & kPinnedFlag) rig.product.settings().peer = bravo::pin_of(kPinnedAddress);
    rig.platform.link().raise_link(kPinnedGo, kPinnedAddress);
    rig.platform.link().raise_link(kStranger, kStrangerAddress);
    rig.seconds_with_fix(1);

    size_t at = 1;
    for (int action = 0; action < kMaxActions && at < size; action++)
        at += act(rig, data + at, size - at);
    return 0;
}
