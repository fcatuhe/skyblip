// The X1's RTC, found on its own bus and driven by nothing.
#include "doctest/doctest.h"
#include "hardware/parts/ysn8900/model.h"
#include "hardware/parts/ysn8900/ysn8900.h"

using namespace skyblip;

TEST_CASE("ysn8900: the RTC is found by its address alone, and nothing is read from it") {
    models::Ysn8900 chip;
    const parts::Ysn8900 rtc{chip};

    CHECK(rtc.answers());
    CHECK(chip.writes == 1);
    CHECK(chip.probes == 1);
    CHECK(chip.reads == 0);
}

TEST_CASE("ysn8900: a bus where nothing answers 0x32 has no RTC") {
    models::Ysn8900 chip;
    chip.answers = false;
    const parts::Ysn8900 rtc{chip};

    CHECK_FALSE(rtc.answers());
}
