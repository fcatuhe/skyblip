// The status dump, formatted: one line per subsystem for a laptop on the USB
// console, and the same table as CBOR frames for the companion page. Both
// surfaces come out of comms::DiagnosticsReport, so this is where it is proved
// that they carry the same numbers under the same names, that neither is ever
// truncated, and that reading one does not disturb anything.
//
// No device and no link here: a snapshot struct in, characters out.
#include <algorithm>
#include <iterator>
#include <string>

#include "core/comms/config.h"  // kSmallestSupportedPayloadBytes: the ceiling both surfaces answer under
#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"
#include "test/support/diagnostics_report.h"

using namespace skyblip;
using namespace skyblip::comms;

TEST_CASE("diagnostics: one line per subsystem, each carrying the counters that already exist") {
    const std::string text = console(busy_device());

    // Uptime, the radio, what it heard, what it knows of itself, then the 1090 MHz receiver.
    CHECK(has(text, "system uptime_s=3725 reset=\"WATCHDOG\" link_drops=2\n"));
    CHECK(has(text,
              "radio noise_dbm=-101 duty_permille=7 rx_ok=1204 rx_bad=37 rx_wait=58 rx_type=19 "
              "rx_unframed=11 rx_miskeyed=7 rx_noise=96 rx_named=12 tx_ok=880 tx_lost=3 "
              "tx_named=41 tx_keyed_us=609 tx_span_us=5919 range_refused=5 dio1_armed=true "
              "dio1_edges=1500 dio1_missed=4 tx_held_for_b5=6 rx_status=9 uplink_stale=2 "
              "tx_given_up=1 tx_deferred=14 tx_forced=1 tx_busy_missed=2\n"));
    CHECK(has(text, "traffic tracked=4 alarm=2\n"));
    CHECK(has(text,
              "gnss fixes=5210 valid=true baud=38400 config=\"READY\" raises=1 raise_fallbacks=0 "
              "solution_phase_ms=98 overruns=0 identified=true "
              "firmware=\"URANUS5,V5.1.0.0\" reject=\"STALE\" rejected=6 residual_m=13\n"));
    CHECK(has(text,
              "power cell_mv=3812 percent=64 battery_valid=true charging=false power_level=\"OK\" "
              "supply_warnings=1 "
              "implausible=2 die_temperature_c=42 charge_warnings=0\n"));
    CHECK(has(text,
              "adsb frames=51234 frame_rate_hz=212 dropped=4 rearms=1 resets=0 command_errors=0 "
              "errors=128 rssi_dbm=0,120,900,3000,2500,800,90,3 crc=3021 unsupported=412 "
              "folded=1830 aircraft=7\n"));

    DiagnosticsReport report(busy_device(), link::Command::Diagnostics);
    CHECK(report.line_count() == DiagnosticsReport::kGroupCount);
    // Past the end is nothing at all, not an empty line a reader would parse.
    char line[DiagnosticsReport::kLineBytes];
    CHECK(report.line(DiagnosticsReport::kGroupCount, line, sizeof(line)) == 0);
    CHECK(report.line(-1, line, sizeof(line)) == 0);
}

// A device on the apron with no fix beside a chatty neighbour reads zero bad frames.
TEST_CASE("diagnostics: the three things that are not a bad reception each have their own key") {
    Diagnostics diagnostics = busy_device();
    diagnostics.rx_bad = 0;

    const std::string text = console(diagnostics);
    CHECK(has(text, "rx_bad=0 rx_wait=58 rx_type=19"));
    CHECK(has(text, "tx_ok=880 tx_lost=3"));

    const std::string json = frames(diagnostics, kSmallestSupportedPayloadBytes).joined;
    CHECK(has(json, "\"rx_bad\":0"));
    CHECK(has(json, "\"rx_wait\":58"));
    CHECK(has(json, "\"rx_type\":19"));
    CHECK(has(json, "\"tx_lost\":3"));
}

// A support case is pasted into an email, so the console form has to stay
// machine-readable: "CPU LOCKUP" is one value, not two fields.
TEST_CASE("diagnostics: a reason is quoted words on the console and its code on the link") {
    Diagnostics diagnostics = busy_device();
    diagnostics.reset = power::ResetReason::Lockup;
    CHECK(has(console(diagnostics), "reset=\"CPU LOCKUP\""));
    CHECK(has(frames(diagnostics, 182).joined, "\"reset\":\"lockup\""));
}

// A cable in the sun is the support case, and this is the only trace it leaves.
TEST_CASE("diagnostics: the charge window reads out only when there is a cable to judge") {
    Diagnostics diagnostics = busy_device();
    CHECK_FALSE(has(console(diagnostics), "charge="));
    CHECK(has(console(diagnostics), "charge_warnings=0"));

    diagnostics.charge = power::ChargeCondition::TooHot;
    diagnostics.charge_warnings = 3;
    CHECK(has(console(diagnostics), "charge=\"HOT\" charge_warnings=3"));
    CHECK(has(frames(diagnostics, 182).joined, "\"charge\":\"too_hot\""));
}

// The buffer the caller lends is a constant in the header, so the widest line
// this dump can ever produce has to be measured rather than assumed.
TEST_CASE("diagnostics: the widest dump every field can produce still fits one console line") {
    const Diagnostics diagnostics = widest_device();
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics);
    int widest = 0;
    char line[DiagnosticsReport::kLineBytes];
    for (int i = 0; i < report.line_count(); i++) {
        const int written = report.line(i, line, static_cast<int>(sizeof(line)));
        REQUIRE(written > 0);
        if (written > widest) widest = written;
    }
    CHECK(widest + 1 <= DiagnosticsReport::kLineBytes);
    // And a buffer one byte short of the line refuses it whole rather than
    // handing back half a counter.
    char tight[DiagnosticsReport::kLineBytes];
    DiagnosticsReport again(diagnostics, link::Command::Diagnostics);
    int longest_index = 0;
    for (int i = 0; i < again.line_count(); i++) {
        if (again.line(i, line, static_cast<int>(sizeof(line))) == widest) longest_index = i;
    }
    CHECK(again.line(longest_index, tight, widest) == 0);
}

TEST_CASE("diagnostics: no reading is no key, on the console as on the link") {
    Diagnostics diagnostics = busy_device();
    diagnostics.residual_valid = false;
    diagnostics.die_valid = false;

    const std::string text = console(diagnostics);
    const std::string json = frames(diagnostics, 182).joined;
    CHECK_FALSE(has(text, "residual_m"));
    CHECK_FALSE(has(text, "die_temperature_c"));
    CHECK_FALSE(has(json, "residual_m"));
    CHECK_FALSE(has(json, "die_temperature_c"));

    // 0 m of residual and 0 C of die temperature are both real readings a device
    // can produce, which is why absent is not spelled as zero.
    diagnostics.residual_valid = true;
    diagnostics.die_valid = true;
    diagnostics.residual_m = 0;
    diagnostics.die_dc = 0;
    CHECK(has(console(diagnostics), "residual_m=0"));
    CHECK(has(console(diagnostics), "die_temperature_c=0"));
    CHECK(has(frames(diagnostics, 182).joined, "\"residual_m\":0"));
    CHECK(has(frames(diagnostics, 182).joined, "\"die_temperature_c\":0"));
}

// The rounding rule is the status reply's, and it is shared code rather than a
// second copy: a support case must not read a different temperature depending on
// which surface answered it.
TEST_CASE("diagnostics: tenths of a degree round away from zero on both sides of freezing") {
    CHECK(whole_celsius(-206) == -21);
    CHECK(whole_celsius(206) == 21);
    CHECK(whole_celsius(-4) == 0);
    CHECK(whole_celsius(0) == 0);
}

TEST_CASE("diagnostics: the receiver's own answers reach the dump, named not numbered") {
    Diagnostics diagnostics = busy_device();
    diagnostics.gnss_identified = false;
    diagnostics.gnss_firmware = "";
    diagnostics.gnss_baud = 9600;
    diagnostics.gnss_config = ports::GnssConfigStage::Degraded;
    diagnostics.gnss_raises = 2;
    diagnostics.gnss_raise_fallbacks = 2;
    diagnostics.gnss_reject = gnss::FixReject::NoDate;
    diagnostics.gnss_rejected = 41;

    const std::string text = console(diagnostics);
    CHECK(has(text, "identified=false"));
    CHECK(has(text, "firmware=\"\""));
    CHECK(has(text, "baud=9600"));
    CHECK(has(text, "config=\"DEGRADED\" raises=2 raise_fallbacks=2"));
    CHECK(has(text, "reject=\"NO DATE\""));
    CHECK(has(text, "rejected=41"));

    // Every reason core/gnss can refuse a solution for has a word, so a support
    // case reads a cause and not an enum value.
    CHECK(std::string(gnss::reject_name(gnss::FixReject::None)) == "NONE");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::NoSolution)) == "NO SOLUTION");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::MissingRmc)) == "NO RMC");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::MissingGga)) == "NO GGA");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::Stale)) == "STALE");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::NoDate)) == "NO DATE");
    CHECK(std::string(gnss::reject_name(gnss::FixReject::Jump)) == "JUMP");
}

// A count is unsigned on the link and on the console, so the widest one is printed whole.
TEST_CASE("diagnostics: a counter is never printed negative, on any platform") {
    Diagnostics diagnostics{};
    diagnostics.rx_ok = 0xFFFFFFFFu;
    diagnostics.gnss_rejected = 0x80000000u;
    diagnostics.link_drops = 0x7FFFFFFFu;
    const std::string text = console(diagnostics);
    CHECK(has(text, "rx_ok=4294967295"));
    CHECK(has(text, "rejected=2147483648"));
    CHECK(has(text, "link_drops=2147483647"));
    // The dBm figures are signed and stay signed; a COUNT never is.
    CHECK(has(text, "noise_dbm=-105"));
    CHECK(has(frames(diagnostics, 182).joined, "\"rx_ok\":4294967295"));
    CHECK(has(frames(diagnostics, 182).joined, "\"noise_dbm\":-105"));
}

// A dump is a witness: one that consumed its evidence would make the second reader wrong.
TEST_CASE("diagnostics: reading the dump neither clears nor changes anything") {
    const Diagnostics diagnostics = busy_device();
    const std::string first = console(diagnostics);
    const std::string first_json = frames(diagnostics, 182).joined;
    const std::string second = console(diagnostics);
    const std::string second_json = frames(diagnostics, 182).joined;
    CHECK(first == second);
    CHECK(first_json == second_json);
    CHECK(diagnostics.rx_ok == 1204);
    CHECK(diagnostics.supply_warnings == 1);
}

// The registry names each key the link carries, and the console prints the same name.
TEST_CASE("diagnostics: the console and the link name every field alike") {
    const Diagnostics diagnostics = widest_device();
    const std::string link = frames(diagnostics, 512).joined;
    const std::string lines = console(diagnostics);
    size_t at = 0;
    int fields = 0;
    while ((at = lines.find('=', at)) != std::string::npos) {
        const size_t start = lines.find_last_of(' ', at) + 1;
        const std::string name = lines.substr(start, at - start);
        CAPTURE(name);
        CHECK(has(link, ("\"" + name + "\":").c_str()));
        fields++;
        at++;
    }
    CHECK(fields == 66);
}
