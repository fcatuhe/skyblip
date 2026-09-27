// What the glass shows of a rate measured once a second: the same number, damped.
#include "core/flight/indicated.h"
#include "doctest/doctest.h"

using namespace skyblip;
using flight::IndicatedRate;

namespace {

constexpr uint32_t kSecondMs = 1000;

IndicatedRate settled_at(int32_t rate) {
    IndicatedRate indicated;
    indicated.observe(rate, kSecondMs);
    return indicated;
}

}  // namespace

TEST_CASE("indicated: the first reading is shown as measured, with nothing to blend it with") {
    IndicatedRate indicated;
    CHECK(indicated.value() == 0);
    indicated.observe(-1500, kSecondMs);
    CHECK(indicated.value() == -1500);
}

TEST_CASE("indicated: a step at one reading a second shows a third, then 70%, then 94%") {
    IndicatedRate indicated = settled_at(0);
    uint32_t t = kSecondMs;
    const auto second = [&] { indicated.observe(3000, t += kSecondMs); };

    second();
    CHECK(indicated.value() == 1000);
    second();
    second();
    // 3000 * (1 - (2/3)^3)
    CHECK(indicated.value() == 2111);
    for (int i = 0; i < 4; i++) second();
    CHECK(indicated.value() == doctest::Approx(2824).epsilon(0.002));
}

TEST_CASE("indicated: a reading that swings every second is shown at a fraction of its swing") {
    IndicatedRate indicated = settled_at(0);
    int32_t widest = 0;
    for (uint32_t i = 1; i <= 30; i++) {
        indicated.observe(i % 2 ? 300 : -300, kSecondMs + i * kSecondMs);
        const int32_t shown = indicated.value() < 0 ? -indicated.value() : indicated.value();
        if (shown > widest) widest = shown;
    }
    CHECK(widest <= 100);
}

TEST_CASE("indicated: the damping is time, so a two-second cadence settles like a one-second one") {
    IndicatedRate every_second = settled_at(0);
    IndicatedRate every_other = settled_at(0);
    for (uint32_t t = 2 * kSecondMs; t <= 7 * kSecondMs; t += kSecondMs)
        every_second.observe(1000, t);
    for (uint32_t t = 3 * kSecondMs; t <= 7 * kSecondMs; t += 2 * kSecondMs)
        every_other.observe(1000, t);

    CHECK(every_second.value() == doctest::Approx(912).epsilon(0.01));
    CHECK(every_other.value() == doctest::Approx(875).epsilon(0.01));
}

TEST_CASE("indicated: a reading after a long silence mostly replaces what was shown") {
    IndicatedRate indicated = settled_at(0);
    indicated.observe(1200, 11 * kSecondMs);
    // ten seconds against two of damping: five sixths of the way
    CHECK(indicated.value() == 1000);
}

TEST_CASE("indicated: after a reset the next reading is shown as measured again") {
    IndicatedRate indicated = settled_at(2000);
    indicated.reset();
    CHECK(indicated.value() == 0);
    indicated.observe(-400, 2 * kSecondMs);
    CHECK(indicated.value() == -400);
}
