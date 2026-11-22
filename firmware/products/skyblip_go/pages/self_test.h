#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SELF_TEST_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_PAGES_SELF_TEST_H

#include <cstdint>
#include <optional>

#include "core/comms/peer.h"
#include "core/timing/transmit_gate.h"
#include "ports/gnss.h"
#include "products/skyblip_go/glass.h"

namespace skyblip::go {

// INFO: fc 20nov26 Absent is not a failure: only a required part missing reads Fail
enum class PartState : uint8_t { Pass, Absent, Fail };

struct SelfTestPart {
    const char* name{""};
    PartState state{PartState::Absent};
    // INFO: fc 20nov26 which part answered a footprint that takes several, never a verdict
    const char* detail{nullptr};
    // INFO: fc 19nov26 how the part works now, after which part answered at boot: CCA OK, 2 HZ
    const char* now{nullptr};
    // INFO: fc 19nov26 the transmit gate names this part's requirement, so the verdict reads NO TX
    bool no_tx{false};
};

struct SelfTestSnapshot {
    std::optional<uint16_t> serial{};
    const char* reset_reason{"UNKNOWN"};
    bool went_dark_flat{false};
    const SelfTestPart* parts{nullptr};
    int part_count{0};
    // INFO: fc 20nov26 a unit missing a required part stays up on this page rather than reboot
    bool flyable{true};
    bool battery_valid{false};
    uint16_t battery_mv{0};
    // INFO: fc 20nov26 the only place a bench sees parts the product never drives, like the RTC
    const uint8_t* i2c_addresses{nullptr};
    const char* const* i2c_roles{nullptr};
    int i2c_address_count{0};
    // INFO: fc 11nov26 go::AircraftAddressWarning::stands(): neither the Go nor Bravo has an
    // aircraft address
    bool aircraft_address_warning{false};
    comms::BravoState bravo{comms::BravoState::Unpaired};
    uint16_t bravo_tag{0};
};

// INFO: fc 20nov26 the bus scan and the paired Bravo take a row each, the warning sharing Bravo's
constexpr int kSelfTestRows = 16;

// INFO: fc 20nov26 exported so a test reads a row back off the glass instead of counting ink
constexpr int kSelfTestLeftX = 4;
constexpr int kSelfTestRightX = kGlassWidth - kSelfTestLeftX;
constexpr int kSelfTestRowCells = (kGlassWidth - 2 * kSelfTestLeftX) / ui::kCellWidth;
constexpr int kSelfTestRowHeight = 9;
constexpr int kSelfTestFirstRowY = 36;
constexpr int kSelfTestHeaderY = 26;

constexpr int self_test_row_y(int row) { return kSelfTestFirstRowY + row * kSelfTestRowHeight; }

constexpr int kSelfTestDividerGap = 3;
constexpr int kSelfTestFooterGap = 5;
constexpr int kSelfTestVerdictRise = 3;
constexpr int kSelfTestVerdictHeight = 13;
constexpr int kSelfTestFooterHeight =
    kSelfTestDividerGap + kSelfTestFooterGap - kSelfTestVerdictRise + kSelfTestVerdictHeight;

static_assert(self_test_row_y(kSelfTestRows) + kSelfTestFooterHeight <= kGlassHeight,
              "the last row would fall off the glass");

constexpr const char* kSelfTestAircraftAddressName = "ADS-B";
constexpr const char* kSelfTestUnsetVerdict = "UNSET";
constexpr const char* kSelfTestNoTxVerdict = "NO TX";

void draw_self_test(ui::Canvas& canvas, const SelfTestSnapshot& snapshot);

// INFO: fc 19nov26 D.3: the radio entering its dwells and reading the channel it listens on
const char* radio_word(const timing::Requirements& met);
// INFO: fc 19nov26 the first of the receiver's requirements unmet, or the 2 Hz it holds
const char* receiver_word(const timing::Requirements& met, const ports::GnssHealth& health);

}  // namespace skyblip::go

#endif
