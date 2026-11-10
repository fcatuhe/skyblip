// A 1090 transmitter's frame as the LR2021's chips after the sync, then noise over them.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/protocol/adsb.h"
#include "test/support/adsb_chips.h"

using namespace skyblip;

namespace {

constexpr uint8_t kExtendedSquitterHead = 17 << 3;
constexpr uint8_t kCapabilityMask = 0x07;

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider in(data, size);
    const bool sealed = in.ConsumeBool();
    uint8_t frame[protocol::kAdsbFrameBytes]{};
    in.ConsumeData(frame, sizeof(frame));
    if (sealed) {
        frame[0] = static_cast<uint8_t>(kExtendedSquitterHead | (frame[0] & kCapabilityMask));
        adsb_seal(frame);
    }

    uint8_t chips[protocol::kAdsbChipBytes];
    adsb_chips(frame, chips);
    const std::vector<uint8_t> noise = in.ConsumeRemainingBytes<uint8_t>();
    for (size_t i = 0; i < noise.size() && i < sizeof(chips); i++) chips[i] ^= noise[i];

    protocol::AdsbMessage message;
    (void)protocol::adsb_receive(chips, sizeof(chips), protocol::AdsbReception{}, message);
    return 0;
}
