// Power-on self test, on the glass. The one page worth having on a first flash:
// it names the part that did not answer, so a device that refuses to fly says
// why instead of going dark.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SELF_TEST_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SELF_TEST_H

#include <cstdint>

#include "core/settings/address.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

// What a part answered at bring-up. Absent is not a failure: a unit built
// without a barometer is a unit without a barometer, and the page has to be able
// to say which of the two it is looking at.
enum class PartState : uint8_t { Pass, Absent, Fail };

struct SelfTestPart {
    const char* name{""};
    PartState state{PartState::Absent};
    // Which part answered, when the footprint takes more than one: the barometer
    // address that replied, the e-paper lot the glass is from, whether the haptic
    // is a waveform driver or a pin. Optional, short (the row has to hold a name,
    // this, and a verdict inside 200 pixels), and never a second verdict.
    const char* detail{nullptr};
};

struct SelfTestSnapshot {
    settings::AirAddress address{};
    const char* reset_reason{"UNKNOWN"};
    bool went_dark_flat{false};
    const SelfTestPart* parts{nullptr};
    int part_count{0};
    // False when a required part is missing. The device stays up and keeps this
    // page: a panel naming the failure is worth more than a reboot loop.
    bool flyable{true};
    bool battery_valid{false};
    uint16_t battery_mv{0};
    // Every address that answered the I2C scan, drawn as one more row. The parts
    // this product deliberately does not drive are in here - the RTC at 0x51, the
    // IMU at 0x28 - and this page is the only place a bench can see them.
    const uint8_t* i2c_addresses{nullptr};
    const char* const* i2c_roles{nullptr};
    int i2c_address_count{0};
};

// The most rows the page can carry before the last one falls off the panel. The
// bus scan's row is one of them.
constexpr int kSelfTestRows = 15;

// The grid the page is set on, exported so a test can read a row back off the
// glass instead of counting ink: "there is ink on the GNSS row" is not the same
// claim as "the GNSS row says FAIL".
constexpr int kSelfTestLeftX = 4;
constexpr int kSelfTestRightX = kGlassWidth - kSelfTestLeftX;
constexpr int kSelfTestRowCells = (kGlassWidth - 2 * kSelfTestLeftX) / ui::kCellW;
constexpr int kSelfTestRowH = 9;
constexpr int kSelfTestFirstRowY = 36;
constexpr int kSelfTestHeaderY = 26;

constexpr int self_test_row_y(int row) { return kSelfTestFirstRowY + row * kSelfTestRowH; }

constexpr int kSelfTestDividerGap = 3;
constexpr int kSelfTestFooterGap = 5;
constexpr int kSelfTestVerdictRise = 3;
constexpr int kSelfTestVerdictH = 13;
constexpr int kSelfTestFooterH =
    kSelfTestDividerGap + kSelfTestFooterGap - kSelfTestVerdictRise + kSelfTestVerdictH;

static_assert(self_test_row_y(kSelfTestRows) + kSelfTestFooterH <= kGlassHeight,
              "the last row would fall off the glass");

void draw_self_test(ui::Canvas& canvas, const SelfTestSnapshot& snapshot);

}  // namespace skyblip::go

#endif
