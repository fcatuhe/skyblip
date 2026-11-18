// A 1090 transmitter's frame as the LR2021 decodes it, then noise over its bits.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocols/adsb/message.h"
#include "protocols/adsb/squitter.h"

using namespace skyblip;

namespace {

constexpr uint8_t kExtendedSquitterHead = 17 << 3;
constexpr uint8_t kCapabilityMask = 0x07;

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider in(data, size);
    const bool sealed = in.ConsumeBool();
    uint8_t frame[adsb::kFrameBytes]{};
    in.ConsumeData(frame, sizeof(frame));
    if (sealed) {
        frame[0] = static_cast<uint8_t>(kExtendedSquitterHead | (frame[0] & kCapabilityMask));
        adsb::seal(frame);
    }

    const std::vector<uint8_t> noise = in.ConsumeRemainingBytes<uint8_t>();
    for (size_t i = 0; i < noise.size() && i < sizeof(frame); i++) frame[i] ^= noise[i];

    adsb::Message message;
    (void)adsb::receive(frame, sizeof(frame), adsb::Reception{}, message);
    return 0;
}
