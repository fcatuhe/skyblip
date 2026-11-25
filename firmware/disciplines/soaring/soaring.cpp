#include "disciplines/soaring/soaring.h"

namespace skyblip::discipline::soaring {

traffic::Level grade(const model::Ownship&, const model::AircraftObservation&, uint32_t) {
    return traffic::Level::None;
}

}  // namespace skyblip::discipline::soaring
