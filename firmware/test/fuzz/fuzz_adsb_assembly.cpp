// A crowd of 1090 transmitters as one receiver hears them, folded aircraft by aircraft.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "core/events/stamp.h"
#include "core/model/aircraft.h"
#include "core/protocol/adsb.h"
#include "core/protocol/adsb_assembly.h"
#include "test/support/adsb_chips.h"

using namespace skyblip;

namespace {

constexpr uint8_t kExtendedSquitterHead = 17 << 3;
constexpr uint8_t kCapabilityMask = 0x07;
constexpr uint8_t kCrowdAddressHigh = 0x40;
constexpr int32_t kQuarterTurn1e7 = 900000000;
constexpr size_t kStepBytes = protocol::kAdsbFrameBytes + sizeof(uint16_t);

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzedDataProvider in(data, size);
    protocol::AdsbReference reference;
    reference.valid = in.ConsumeBool();
    reference.position.latitude_1e7 =
        in.ConsumeIntegralInRange<int32_t>(-kQuarterTurn1e7, kQuarterTurn1e7);
    reference.position.longitude_1e7 = in.ConsumeIntegralInRange<int32_t>(-1800000000, 1799999999);
    protocol::AdsbAssembly assembly;
    assembly.set_own_address((uint32_t{kCrowdAddressHigh} << 16) | in.ConsumeIntegral<uint8_t>());
    uint32_t at_ms = in.ConsumeIntegral<uint32_t>();
    uint32_t lost = 0;

    while (in.remaining_bytes() >= kStepBytes) {
        at_ms += in.ConsumeIntegral<uint16_t>();
        assembly.age_out(at_ms);
        uint8_t frame[protocol::kAdsbFrameBytes]{};
        in.ConsumeData(frame, sizeof(frame));
        frame[0] = static_cast<uint8_t>(kExtendedSquitterHead | (frame[0] & kCapabilityMask));
        frame[1] = kCrowdAddressHigh;
        frame[2] = 0;
        adsb_seal(frame);
        uint8_t chips[protocol::kAdsbChipBytes];
        adsb_chips(frame, chips);

        protocol::AdsbMessage message;
        if (protocol::adsb_receive(chips, sizeof(chips), protocol::AdsbReception{}, message) !=
            Status::Ok)
            continue;
        model::AircraftObservation observation;
        const bool folded = assembly.fold(message, at_ms, events::Stamp{}, reference, observation);
        if (folded && observation.position_valid &&
            (observation.latitude_1e7 > kQuarterTurn1e7 ||
             observation.latitude_1e7 < -kQuarterTurn1e7))
            std::abort();
        const uint32_t lost_now = assembly.evictions() + assembly.refusals();
        if (lost_now < lost || assembly.count() > protocol::AdsbAssembly::kMaxAircraft)
            std::abort();
        lost = lost_now;
    }
    return 0;
}
