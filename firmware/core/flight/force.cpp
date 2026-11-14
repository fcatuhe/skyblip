#include "core/flight/force.h"

#include "protocols/common/geometry.h"

namespace skyblip::flight {

int32_t resultant_mg(const SpecificForce& force) {
    const int64_t square = static_cast<int64_t>(force.right_mg) * force.right_mg +
                           static_cast<int64_t>(force.up_mg) * force.up_mg +
                           static_cast<int64_t>(force.aft_mg) * force.aft_mg;
    return static_cast<int32_t>(wire::square_root<uint64_t>(static_cast<uint64_t>(square)));
}

}  // namespace skyblip::flight
