// Two companion apps writing to a parked device, through the platform's link into the product.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/events/link.h"
#include "products/skyblip_go/input/gesture.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 25sep26 the panel's modelled boot refresh is 95% of setup, and no write needs a panel
constexpr ports::Capabilities kNoGlass =
    static_cast<ports::Capabilities>(static_cast<uint32_t>(platform::host::Platform::kFullyFitted) &
                                     ~static_cast<uint32_t>(ports::Capability::Display));

constexpr uint16_t kFirstApp = 1;
constexpr uint16_t kSecondApp = 2;
constexpr uint32_t kGroundSeconds = 1;
constexpr uint32_t kWriteGapMs = 100;
constexpr int kMaxActions = 16;
constexpr size_t kWriteHeadBytes = 3;

constexpr int32_t kTaxiMmS = 0;
constexpr int32_t kCruiseMmS = 50000;
constexpr int32_t kFieldAltitudeM = 300;
constexpr int32_t kCruiseAltitudeM = 800;
constexpr uint32_t kTaxiOutSeconds = 20;
constexpr uint32_t kFlightSeconds = 80;
constexpr uint32_t kTaxiInSeconds = 40;

// One flight flown once, so every input boots onto a log partition with a session to read.
const std::vector<uint8_t>& logbook() {
    static const std::vector<uint8_t> image = [] {
        Rig flight(kNoGlass);
        if (flight.setup() != Status::Ok) __builtin_trap();
        uint32_t now_ms = 0;
        flight.seconds(now_ms, kTaxiOutSeconds, kTaxiMmS, kFieldAltitudeM);
        flight.seconds(now_ms, kFlightSeconds, kCruiseMmS, kCruiseAltitudeM);
        flight.seconds(now_ms, kTaxiInSeconds, kTaxiMmS, kFieldAltitudeM);
        return flight.platform.log_flash().bytes();
    }();
    return image;
}

// INFO: fc 25sep26 a longer write is refused by the ATT layer (hardware/platform/zephyr/link.cpp)
void write(Rig& rig, uint16_t session_id, events::Endpoint endpoint, const uint8_t* bytes,
           size_t byte_count) {
    events::RxFrame frame{};
    if (byte_count > frame.data.size()) return;
    frame.session_id = session_id;
    frame.endpoint = endpoint;
    frame.bytes = static_cast<uint16_t>(byte_count);
    std::memcpy(frame.data.data(), bytes, byte_count);
    rig.platform.link().push_rx(frame);
}

// The gesture arms once the prompt has stood as long as a double press takes.
void confirm_on_device(Rig& rig, uint32_t& now_ms) {
    rig.run(now_ms, now_ms + go::ConfirmGesture::kDoublePressWindowMs);
    now_ms += go::ConfirmGesture::kDoublePressWindowMs;
    rig.double_press(now_ms);
}

size_t act(Rig& rig, uint32_t& now_ms, const uint8_t* input, size_t left) {
    const uint8_t action = input[0];
    if (action == 'P') {
        confirm_on_device(rig, now_ms);
        return 1;
    }
    if (left < kWriteHeadBytes) return left;
    const size_t declared = static_cast<size_t>(input[1] | (input[2] << 8));
    const size_t frame_bytes =
        declared < left - kWriteHeadBytes ? declared : left - kWriteHeadBytes;
    const uint8_t* frame = input + kWriteHeadBytes;
    switch (action) {
        case 'C': write(rig, kFirstApp, events::Endpoint::Config, frame, frame_bytes); break;
        case 'c': write(rig, kSecondApp, events::Endpoint::Config, frame, frame_bytes); break;
        case 'L': write(rig, kFirstApp, events::Endpoint::Log, frame, frame_bytes); break;
        case 'l': write(rig, kSecondApp, events::Endpoint::Log, frame, frame_bytes); break;
        default: return 1;
    }
    rig.run(now_ms, now_ms + kWriteGapMs);
    now_ms += kWriteGapMs;
    return kWriteHeadBytes + frame_bytes;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Rig rig(kNoGlass);
    rig.platform.log_flash().restore(logbook());
    if (rig.setup() != Status::Ok) __builtin_trap();
    rig.utc_offset_s = kTaxiOutSeconds + kFlightSeconds + kTaxiInSeconds;
    uint32_t now_ms = 0;
    rig.seconds(now_ms, kGroundSeconds, kTaxiMmS, kFieldAltitudeM);
    rig.raise_link(kFirstApp);
    rig.raise_link(kSecondApp);

    size_t at = 0;
    for (int action = 0; action < kMaxActions && at < size; action++)
        at += act(rig, now_ms, data + at, size - at);
    return 0;
}
