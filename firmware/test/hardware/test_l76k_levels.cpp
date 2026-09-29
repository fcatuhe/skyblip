// Satellites in view: the GSV sets the sats page reads, and what they cost the fix beside them.
#include <string>

#include "doctest/doctest.h"
#include "hardware/parts/l76k/l76k.h"
#include "hardware/parts/l76k/model.h"
#include "test/support/l76k_rig.h"

using namespace skyblip;

namespace {
// The wire as the driver reads it, kept so a test can see where in a burst RMC fell.
class TappedWire : public io::Uart {
   public:
    explicit TappedWire(models::L76k& chip) : chip_(chip) {}

    size_t write(const uint8_t* data, size_t len) override { return chip_.write(data, len); }

    size_t read(uint8_t* out, size_t cap) override {
        const size_t n = chip_.read(out, cap);
        heard_.append(reinterpret_cast<const char*>(out), n);
        return n;
    }

    size_t available() override { return chip_.available(); }

    std::string take() {
        std::string burst;
        burst.swap(heard_);
        return burst;
    }

   private:
    models::L76k& chip_;
    std::string heard_;
};

struct Burst {
    uint32_t ahead_of_fix_bytes{0};
    bool carried_levels{false};
};

Burst read_burst(const std::string& wire) {
    Burst b{};
    const size_t rmc = wire.find("$GPRMC");
    if (rmc == std::string::npos) return b;
    b.ahead_of_fix_bytes = static_cast<uint32_t>(wire.find('\n', rmc) + 1);
    b.carried_levels = wire.find("GSV,") < rmc;
    return b;
}

uint8_t in_view(const models::L76k& chip) {
    return static_cast<uint8_t>(chip.gps_in_view + chip.beidou_in_view + chip.glonass_in_view);
}
}  // namespace

// The levels cost a set of sentences, so only the page that draws them asks for them.
TEST_CASE("l76k: satellites in view are asked for every solution until a set lands, then paced") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    REQUIRE(gnss.configured());
    REQUIRE_FALSE(chip.gsv_enabled());  // the bring-up sentence set switches it off
    REQUIRE(gnss.sky().count() == 0);

    const uint32_t commands = chip.commands_seen;
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);
    CHECK(chip.commands_seen == commands + 2);
    CHECK(chip.gsv_every == parts::L76k::kSatellitesInViewPeriodSolutions);
    CHECK(gnss.satellites_in_view_live());
    CHECK(gnss.sky().count() == in_view(chip));
    CHECK(gnss.sky().in_view_of(gnss::System::Gps) == chip.gps_in_view);
    CHECK(gnss.sky().in_view_of(gnss::System::Beidou) == chip.beidou_in_view);

    // Nothing else in the sentence set moved: the nulls in $PCAS03 mean "keep".
    CHECK(chip.gga_enabled);
    CHECK(chip.rmc_enabled);
    CHECK(chip.gsa_enabled);
    CHECK_FALSE(chip.gll_enabled);
    CHECK_FALSE(chip.vtg_enabled);

    gnss.request_satellites_in_view(false);
    run(gnss, chip, kBringUpLeadMs + 7010, kBringUpLeadMs + 9000);
    CHECK(chip.commands_seen == commands + 3);
    CHECK_FALSE(chip.gsv_enabled());
    CHECK_FALSE(gnss.satellites_in_view_live());

    // Asking again for what is already so costs nothing.
    gnss.request_satellites_in_view(false);
    run(gnss, chip, kBringUpLeadMs + 9010, kBringUpLeadMs + 11000);
    CHECK(chip.commands_seen == commands + 3);
}

// The pinned bug: a page 0 capture read 32 in view, the last set the sats page had asked for.
TEST_CASE("l76k: given up, the satellites in view are forgotten, not kept at the last set") {
    models::L76k chip;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);
    REQUIRE(gnss.sky().count() == in_view(chip));

    gnss.request_satellites_in_view(false);
    run(gnss, chip, kBringUpLeadMs + 7010, kBringUpLeadMs + 9000);
    CHECK(gnss.sky().count() == 0);
    CHECK_FALSE(gnss.levels_fresh());
    CHECK(gnss.sky().in_use() > 0);  // GSA still names the solution every second

    chip.gps_in_view = 4;
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 9010, kBringUpLeadMs + 11000);
    CHECK(gnss.sky().count() == in_view(chip));
}

// GSV rides between GSA and RMC, which is why the bench read nav_ms 55-65 ms late with sats up.
TEST_CASE("l76k: with the levels up, four fixes in five close on the fix burst alone") {
    models::L76k chip;
    TappedWire wire(chip);
    parts::L76k gnss(wire, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    REQUIRE(gnss.configured());
    REQUIRE(gnss.baud_rate() == parts::L76k::kTargetBaudRate);
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);
    REQUIRE(chip.gsv_every == parts::L76k::kSatellitesInViewPeriodSolutions);
    wire.take();

    int with_levels = 0;
    int clean = 0;
    uint32_t paced_ms = 0;
    for (uint32_t t = kBringUpLeadMs + 7010; t < kBringUpLeadMs + 17010; t += 1000) {
        run(gnss, chip, t, t + 990);
        const Burst b = read_burst(wire.take());
        const uint32_t fix_ms = parts::wire_ms(b.ahead_of_fix_bytes, gnss.baud_rate());
        if (b.carried_levels) {
            with_levels++;
            paced_ms = fix_ms;
        } else if (fix_ms <= parts::L76k::kBurstMs) {
            clean++;
        }
        CHECK(gnss.sky().count() == in_view(chip));
        CHECK(gnss.levels_fresh() == b.carried_levels);
    }

    CHECK(with_levels == 2);
    CHECK(clean == 8);
    // The model's 21 satellites are seven GSV sentences, 413 bytes: 35 ms of 115200 ahead of RMC.
    CHECK(paced_ms > parts::L76k::kBurstMs + 30);
}

// At the rate the receiver boots at, the widest GSV set did not fit in the second the fix rides.
TEST_CASE("l76k: the satellites-in-view burst fits the second only at the raised rate") {
    CHECK(parts::wire_ms(parts::L76k::kSearchingBurstBytes, parts::L76k::kBaudRate) >
          parts::L76k::kSolutionPeriodMs);
    CHECK(parts::L76k::kSearchingBurstMs < parts::L76k::kSolutionPeriodMs);
    CHECK(parts::L76k::kSolutionPeriodMs == parts::L76k::kFixPeriodMs);
}

// A receiver still searching reports satellites in view it is not yet tracking.
TEST_CASE("l76k: a satellite in view with no level is not a satellite at zero dB-Hz") {
    models::L76k chip;
    chip.fix = false;
    parts::L76k gnss(chip, chip);
    run(gnss, chip, 0, kBringUpLeadMs + 4000);
    gnss.request_satellites_in_view(true);
    run(gnss, chip, kBringUpLeadMs + 4010, kBringUpLeadMs + 7000);

    REQUIRE(gnss.sky().count() > 0);
    int tracked = 0, silent = 0;
    for (int i = 0; i < gnss.sky().count(); i++)
        (gnss.sky().at(i).cn0_dbhz > 0 ? tracked : silent)++;
    CHECK(tracked > 0);
    CHECK(silent > 0);
    CHECK(gnss.sky().in_use() == 0);  // nothing solved, so nothing is in a solution
}
