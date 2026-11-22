// The status dump as CBOR frames for the companion page: a snapshot struct in, frames out.
#include <string>

#include "core/comms/config.h"
#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"
#include "test/support/diagnostics_report.h"

using namespace skyblip;
using namespace skyblip::comms;

TEST_CASE("diagnostics: one group per subsystem, each carrying the counters that already exist" *
          doctest::test_suite("main")) {
    const std::string json = frames(busy_device(), 512).joined;

    CHECK(has(json, "\"group\":\"system\""));
    CHECK(has(json, "\"uptime_s\":3725"));
    CHECK(has(json, "\"link_drops\":2"));
    CHECK(has(json, "\"group\":\"radio\""));
    CHECK(has(json, "\"noise_dbm\":-101"));
    CHECK(has(json, "\"tx_busy_missed\":2"));
    CHECK(has(json, "\"tx_held_for_fix\":4"));
    CHECK(has(json, "\"tx_redrawn_for_fix\":9"));
    CHECK(has(json, "\"no_tx\":\"pps\""));
    CHECK(has(json, "\"group\":\"traffic\""));
    CHECK(has(json, "\"uplink_targets\":52"));
    CHECK(has(json, "\"group\":\"gnss\""));
    CHECK(has(json, "\"firmware\":\"URANUS5,V5.1.0.0\""));
    CHECK(has(json, "\"residual_m\":13"));
    CHECK(has(json, "\"group\":\"power\""));
    CHECK(has(json, "\"die_temperature_c\":42"));
    CHECK(has(json, "\"group\":\"adsb\""));
    CHECK(has(json, "\"rssi_dbm\":[0,120,900,3000,2500,800,90,3]"));
    CHECK(has(json, "\"group\":\"capture\""));
    CHECK(has(json, "\"keeps_power_run_s\":433500"));
}

// A Go that transmits has nothing to say why it does not, so the key is absent, not none.
TEST_CASE("diagnostics: no_tx is in the radio group while a requirement is unmet, absent after") {
    Diagnostics diagnostics = busy_device();
    diagnostics.no_tx = timing::NoTx::Radio;
    CHECK(has(frames(diagnostics, 512).joined, "\"no_tx\":\"radio\""));
    diagnostics.no_tx = timing::NoTx::None;
    CHECK_FALSE(has(frames(diagnostics, 512).joined, "no_tx"));
}

// A device on the apron with no fix beside a chatty neighbour reads zero bad frames.
TEST_CASE("diagnostics: the three things that are not a bad reception each have their own key") {
    Diagnostics diagnostics = busy_device();
    diagnostics.rx_bad = 0;

    const std::string json = frames(diagnostics, kSmallestSupportedPayloadBytes).joined;
    CHECK(has(json, "\"rx_bad\":0"));
    CHECK(has(json, "\"rx_wait\":58"));
    CHECK(has(json, "\"rx_type\":19"));
    CHECK(has(json, "\"tx_lost\":3"));
}

TEST_CASE("diagnostics: a reason travels as its code") {
    Diagnostics diagnostics = busy_device();
    diagnostics.reset = power::ResetReason::Lockup;
    CHECK(has(frames(diagnostics, 182).joined, "\"reset\":\"lockup\""));
}

// A cable in the sun is the support case, and this is the only trace it leaves.
TEST_CASE("diagnostics: the charge window reads out only when there is a cable to judge") {
    Diagnostics diagnostics = busy_device();
    CHECK_FALSE(has(frames(diagnostics, 182).joined, "\"charge\":"));
    CHECK(has(frames(diagnostics, 182).joined, "\"charge_warnings\":0"));

    diagnostics.charge = power::ChargeCondition::TooHot;
    diagnostics.charge_warnings = 3;
    const std::string json = frames(diagnostics, 182).joined;
    CHECK(has(json, "\"charge\":\"too_hot\""));
    CHECK(has(json, "\"charge_warnings\":3"));
}

// 0 m of residual and 0 C of die temperature are real readings, so absent is not spelled as zero.
TEST_CASE("diagnostics: no reading is no key") {
    Diagnostics diagnostics = busy_device();
    diagnostics.residual_valid = false;
    diagnostics.die_valid = false;

    const std::string json = frames(diagnostics, 182).joined;
    CHECK_FALSE(has(json, "residual_m"));
    CHECK_FALSE(has(json, "die_temperature_c"));

    diagnostics.residual_valid = true;
    diagnostics.die_valid = true;
    diagnostics.residual_m = 0;
    diagnostics.die_dc = 0;
    CHECK(has(frames(diagnostics, 182).joined, "\"residual_m\":0"));
    CHECK(has(frames(diagnostics, 182).joined, "\"die_temperature_c\":0"));
}

// The status reply shares this rounding, so both replies read a support case alike.
// A cold morning must not read one degree warmer than it is, nor a hot afternoon one cooler.
TEST_CASE("diagnostics: tenths of a degree round away from zero on both sides of freezing") {
    CHECK(whole_celsius(-206) == -21);
    CHECK(whole_celsius(206) == 21);
    CHECK(whole_celsius(-4) == 0);
    CHECK(whole_celsius(0) == 0);
}

TEST_CASE("diagnostics: the receiver's own answers reach the dump") {
    Diagnostics diagnostics = busy_device();
    diagnostics.gnss_identified = false;
    diagnostics.gnss_firmware = "";
    diagnostics.gnss_baud = 9600;
    diagnostics.gnss_config = ports::GnssConfigStage::Degraded;
    diagnostics.gnss_raises = 2;
    diagnostics.gnss_raise_fallbacks = 2;
    diagnostics.gnss_reject = gnss::FixReject::NoDate;
    diagnostics.gnss_rejected = 41;

    const std::string json = frames(diagnostics, 182).joined;
    CHECK(has(json, "\"identified\":false"));
    CHECK(has(json, "\"firmware\":\"\""));
    CHECK(has(json, "\"baud\":9600"));
    CHECK(has(json, "\"raises\":2"));
    CHECK(has(json, "\"raise_fallbacks\":2"));
    CHECK(has(json, "\"rejected\":41"));
}

// A count is unsigned on the link, so the widest one travels whole.
TEST_CASE("diagnostics: a counter is never sent negative, on any platform") {
    Diagnostics diagnostics{};
    diagnostics.rx_ok = 0xFFFFFFFFu;
    diagnostics.gnss_rejected = 0x80000000u;
    diagnostics.link_drops = 0x7FFFFFFFu;
    const std::string json = frames(diagnostics, 182).joined;
    CHECK(has(json, "\"rx_ok\":4294967295"));
    CHECK(has(json, "\"rejected\":2147483648"));
    CHECK(has(json, "\"link_drops\":2147483647"));
    CHECK(has(json, "\"noise_dbm\":-105"));
}

// A dump is a witness: one that consumed its evidence would make the second reader wrong.
TEST_CASE("diagnostics: reading the dump neither clears nor changes anything") {
    const Diagnostics diagnostics = busy_device();
    const std::string first = frames(diagnostics, 182).joined;
    const std::string second = frames(diagnostics, 182).joined;
    CHECK(first == second);
    CHECK(diagnostics.rx_ok == 1204);
    CHECK(diagnostics.supply_warnings == 1);
}
