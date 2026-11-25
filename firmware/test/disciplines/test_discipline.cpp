// Which discipline own-ship flies, and what each pack adds to the ring's grade.
#include "core/model/aircraft_category.h"
#include "core/traffic/alarm.h"
#include "disciplines/discipline.h"
#include "disciplines/soaring/soaring.h"
#include "doctest/doctest.h"
#include "test/support/traffic_scene.h"

using namespace skyblip;
using discipline::Discipline;
using model::AircraftCategory;

namespace {

uint8_t code(AircraftCategory category) { return static_cast<uint8_t>(category); }

}  // namespace

// Gliders, hang gliders and paragliders all circle in thermals and share them, so one pack serves
// the three, and reads each target's category where they differ.
TEST_CASE("discipline: a glider, a hang glider and a paraglider fly soaring") {
    CHECK(discipline::discipline_of(code(AircraftCategory::Glider)) == Discipline::Soaring);
    CHECK(discipline::discipline_of(code(AircraftCategory::HangGlider)) == Discipline::Soaring);
    CHECK(discipline::discipline_of(code(AircraftCategory::Paraglider)) == Discipline::Soaring);
}

TEST_CASE("discipline: an aircraft no pack serves flies none, and the ring alone grades it") {
    CHECK(discipline::discipline_of(code(AircraftCategory::LightFixedWing)) == Discipline::None);
    CHECK(discipline::discipline_of(code(AircraftCategory::Paramotor)) == Discipline::None);
    CHECK(discipline::discipline_of(18) == Discipline::None);

    model::Ownship own = flying(50, 0);
    own.aircraft_category = code(AircraftCategory::LightFixedWing);
    CHECK(discipline::grade(own, target_at(own, 1200, 0, 0, 50, 180), 0) == traffic::Level::None);
}

TEST_CASE("soaring: raises no aircraft until the pack decides one") {
    model::Ownship own = flying(25, 0, 12);
    own.aircraft_category = code(AircraftCategory::Glider);
    const model::AircraftObservation circling = target_at(own, 600, 0, 50, 25, 270);
    CHECK(discipline::soaring::grade(own, circling, 0) == traffic::Level::None);
    CHECK(discipline::grade(own, circling, 0) == traffic::Level::None);
}
