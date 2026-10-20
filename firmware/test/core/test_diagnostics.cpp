// The status dump, formatted: one line per subsystem for a laptop on the USB
// console, and the same table as CBOR frames for the companion page. Both
// surfaces come out of comms::DiagnosticsReport, so this is where it is proved
// that they carry the same numbers under the same names, that neither is ever
// truncated, and that reading one does not disturb anything.
//
// No device and no link here: a snapshot struct in, characters out.
#include <string>

#include "core/comms/config.h"  // kSmallestSupportedPayloadBytes: the ceiling both surfaces answer under
#include "core/comms/diagnostics.h"
#include "core/comms/link_registry.h"
#include "doctest/doctest.h"
#include "test/support/config_frame.h"

using namespace skyblip;
using namespace skyblip::comms;

namespace {

// A device that has been up for a while and has something to complain about, so
// that no field reads as its default.
Diagnostics busy_device() {
    Diagnostics diagnostics{};
    diagnostics.refreshes = 12;
    diagnostics.uptime_s = 3725;
    diagnostics.reset = power::ResetReason::Watchdog;
    diagnostics.link_drops = 2;
    diagnostics.mtu_exchanged_by_central = 6;
    diagnostics.mtu_exchanged_by_unit = 4;
    diagnostics.mtu_never_exchanged = diagnostics.mtu_requests_failed = 1;
    diagnostics.replies_too_wide = 3;

    diagnostics.noise_dbm = -101;
    diagnostics.duty_permille = 7;
    diagnostics.rx_ok = 1204;
    diagnostics.rx_bad = 37;
    diagnostics.rx_wait = 58;
    diagnostics.rx_type = 19;
    diagnostics.rx_unframed = 11;
    diagnostics.rx_miskeyed = 7;
    diagnostics.rx_noise = 96;
    diagnostics.rx_named = 12;
    diagnostics.tx_ok = 880;
    diagnostics.tx_lost = 3;
    diagnostics.tx_named = 41;
    diagnostics.tx_keyed_us = 609;
    diagnostics.tx_span_us = 5919;
    diagnostics.range_refused = 5;
    diagnostics.dio1_armed = true;
    diagnostics.dio1_edges = 1500;
    diagnostics.dio1_missed = 4;

    diagnostics.tracked = 4;
    diagnostics.alarm = 2;

    diagnostics.gnss_fixes = 5210;
    diagnostics.fix_valid = true;
    diagnostics.gnss_baud = 38400;
    diagnostics.gnss_config = ports::GnssConfigStage::Ready;
    diagnostics.gnss_raises = 1;
    diagnostics.gnss_identified = true;
    diagnostics.gnss_firmware = "URANUS5,V5.1.0.0";
    diagnostics.gnss_reject = gnss::FixReject::Stale;
    diagnostics.gnss_rejected = 6;
    diagnostics.gnss_overruns = 0;
    diagnostics.gnss_solution_phase_ms = 98;
    diagnostics.gnss_solution_phase_valid = true;
    diagnostics.residual_m = 13;
    diagnostics.residual_valid = true;

    diagnostics.battery.cell_mv = 3812;
    diagnostics.battery.percent = 64;
    diagnostics.battery.valid = true;
    diagnostics.battery.charging = false;
    diagnostics.power_level = power::PowerLevel::Normal;
    diagnostics.supply_warnings = 1;
    diagnostics.battery_implausible = 2;
    diagnostics.die_dc = 415;
    diagnostics.die_valid = true;
    return diagnostics;
}

// Every counter at the widest value the dump carries, every string at the longest it can hold.
Diagnostics widest_device() {
    Diagnostics diagnostics{};
    diagnostics.refreshes = 0xFFFFFFFFu;
    diagnostics.uptime_s = 0xFFFFFFFFu;
    diagnostics.reset = power::ResetReason::Lockup;  // "CPU LOCKUP"
    diagnostics.link_drops = diagnostics.mtu_exchanged_by_central = 0xFFFFFFFFu;
    diagnostics.mtu_exchanged_by_unit = diagnostics.mtu_never_exchanged = 0xFFFFFFFFu;
    diagnostics.mtu_requests_failed = diagnostics.replies_too_wide = 0xFFFFFFFFu;
    diagnostics.noise_dbm = -128;
    diagnostics.duty_permille = 1000;
    diagnostics.rx_ok = 0xFFFFFFFFu;
    diagnostics.rx_bad = 0xFFFFFFFFu;
    diagnostics.rx_wait = 0xFFFFFFFFu;
    diagnostics.rx_type = 0xFFFFFFFFu;
    diagnostics.rx_unframed = 0xFFFFFFFFu;
    diagnostics.rx_miskeyed = 0xFFFFFFFFu;
    diagnostics.rx_noise = 0xFFFFFFFFu;
    diagnostics.rx_named = 0xFFFFFFFFu;
    diagnostics.tx_ok = 0xFFFFFFFFu;
    diagnostics.tx_lost = 0xFFFFFFFFu;
    diagnostics.tx_named = 0xFFFFFFFFu;
    diagnostics.tx_keyed_us = 65535;
    diagnostics.tx_span_us = 65535;
    diagnostics.range_refused = 0xFFFFFFFFu;
    diagnostics.dio1_armed = false;
    diagnostics.dio1_edges = 0xFFFFFFFFu;
    diagnostics.dio1_missed = 0xFFFFFFFFu;
    diagnostics.tracked = 0xFFFFFFFFu;
    diagnostics.alarm = 255;
    diagnostics.gnss_fixes = 0xFFFFFFFFu;
    diagnostics.gnss_baud = 921600;
    diagnostics.gnss_config = ports::GnssConfigStage::Degraded;  // "DEGRADED"
    diagnostics.gnss_raises = 0xFFFFFFFFu;
    diagnostics.gnss_raise_fallbacks = 0xFFFFFFFFu;
    // The driver's version buffer is 24 bytes, so 23 characters is the longest
    // string a receiver can ever hand us (core/gnss/nmea.h::kVersionBytes).
    diagnostics.gnss_firmware = "ABCDEFGHIJKLMNOPQRSTUVW";
    diagnostics.gnss_reject = gnss::FixReject::NoSolution;  // "NO SOLUTION"
    diagnostics.gnss_rejected = 0xFFFFFFFFu;
    diagnostics.gnss_overruns = 0xFFFFFFFFu;
    diagnostics.gnss_solution_phase_ms = 999;
    diagnostics.gnss_solution_phase_valid = true;
    diagnostics.residual_m = 65535;
    diagnostics.residual_valid = true;
    diagnostics.battery.cell_mv = 65535;
    diagnostics.battery.percent = 255;
    diagnostics.power_level = power::PowerLevel::Critical;  // "CRITICAL"
    diagnostics.supply_warnings = 0xFFFFFFFFu;
    diagnostics.battery_implausible = 0xFFFFFFFFu;
    diagnostics.die_dc = -1250;
    diagnostics.die_valid = true;
    diagnostics.charge = power::ChargeCondition::TooCold;
    diagnostics.charge_warnings = 0xFFFFFFFFu;
    return diagnostics;
}

std::string console(const Diagnostics& diagnostics) {
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics);
    std::string all;
    char line[DiagnosticsReport::kLineBytes];
    for (int i = 0; i < report.line_count(); i++) {
        const int written = report.line(i, line, static_cast<int>(sizeof(line)));
        REQUIRE(written > 0);
        CHECK(static_cast<int>(std::string(line).size()) == written);
        all += line;
        all += "\n";
    }
    return all;
}

// Every frame the report produces at a given payload, joined, plus a count.
struct Frames {
    std::string joined;
    int count{0};
    int widest{0};
    std::string last;
};

std::string text(const uint8_t* frame, int bytes) {
    return link_text(std::string(reinterpret_cast<const char*>(frame), static_cast<size_t>(bytes)));
}

Frames frames(const Diagnostics& diagnostics, int payload,
              link::Command command = link::Command::Diagnostics) {
    DiagnosticsReport report(diagnostics, command);
    Frames out;
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    while (!report.exhausted()) {
        const int written = report.next_frame(payload, buffer, static_cast<int>(sizeof(buffer)));
        REQUIRE(written > 0);
        REQUIRE(written <= payload);
        out.last = text(buffer, written);
        REQUIRE(!out.last.empty());
        out.joined += out.last;
        if (written > out.widest) out.widest = written;
        out.count++;
        REQUIRE(out.count < 32);  // a dump that never ends is a bug, not a report
    }
    return out;
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("diagnostics: one line per subsystem, each carrying the counters that already exist") {
    const std::string text = console(busy_device());

    // Five subsystems, in the order a bench eye wants them: how long it has been
    // up, then the radio, then what it heard, then what it knows about itself.
    CHECK(has(text,
              "system uptime_s=3725 reset=\"WATCHDOG\" link_drops=2 mtu_exchanged_by_central=6 "
              "mtu_exchanged_by_unit=4 mtu_never_exchanged=1 mtu_requests_failed=1 "
              "replies_too_wide=3\n"));
    CHECK(has(text,
              "radio noise_dbm=-101 duty_permille=7 rx_ok=1204 rx_bad=37 rx_wait=58 rx_type=19 "
              "rx_unframed=11 rx_miskeyed=7 rx_noise=96 rx_named=12 tx_ok=880 tx_lost=3 "
              "tx_named=41 tx_keyed_us=609 tx_span_us=5919 range_refused=5 dio1_armed=true "
              "dio1_edges=1500 dio1_missed=4\n"));
    CHECK(has(text, "traffic tracked=4 alarm=2\n"));
    CHECK(has(text,
              "gnss fixes=5210 valid=true baud=38400 config=\"READY\" raises=1 raise_fallbacks=0 "
              "solution_phase_ms=98 overruns=0 identified=true "
              "firmware=\"URANUS5,V5.1.0.0\" reject=\"STALE\" rejected=6 residual_m=13\n"));
    CHECK(has(text,
              "power cell_mv=3812 percent=64 battery_valid=true charging=false power_level=\"OK\" "
              "supply_warnings=1 "
              "implausible=2 die_temperature_c=42 charge_warnings=0\n"));

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

// The narrowest phone in the field carries 182 bytes of notification payload, and
// a frame one byte over that is not shortened by the controller - it fails. So the
// dump is cut into frames the payload can carry, and every frame is a complete
// object naming the subsystem it belongs to.
TEST_CASE("diagnostics: the link form is frames the negotiated payload can carry, whole") {
    const Diagnostics diagnostics = widest_device();
    const Frames iphone = frames(diagnostics, kSmallestSupportedPayloadBytes);
    CHECK(iphone.widest <= kSmallestSupportedPayloadBytes);
    CHECK(iphone.count == DiagnosticsReport::kGroupCount);
    const Frames narrow = frames(diagnostics, 64);
    CHECK(narrow.widest <= 64);
    CHECK(narrow.count > DiagnosticsReport::kGroupCount);  // the widest values need a second pass
    CHECK(has(narrow.last, "\"more\":false"));
    // Only the last frame says there is nothing after it.
    size_t at = 0;
    int finals = 0;
    while ((at = narrow.joined.find("\"more\":false", at)) != std::string::npos) {
        finals++;
        at += 1;
    }
    CHECK(finals == 1);

    // A link wide enough for everything answers in one frame per subsystem, and
    // never fewer: a reader that only wants the radio stops at its group.
    const Frames wide = frames(busy_device(), 512);
    CHECK(wide.count == DiagnosticsReport::kGroupCount);
    CHECK(has(wide.joined, "\"group\":\"system\""));
    CHECK(has(wide.joined, "\"group\":\"radio\""));
    CHECK(has(wide.joined, "\"group\":\"traffic\""));
    CHECK(has(wide.joined, "\"group\":\"gnss\""));
    CHECK(has(wide.joined, "\"group\":\"power\""));
}

TEST_CASE("diagnostics: a frame never mixes two subsystems") {
    DiagnosticsReport report(widest_device(), link::Command::Diagnostics);
    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    while (!report.exhausted()) {
        const int written =
            report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer));
        REQUIRE(written > 0);
        const std::string body = text(buffer, written);
        // The radio's keys cannot appear in a frame that says it is the receiver's,
        // whichever way round the report packed them.
        if (has(body, "\"group\":\"gnss\"")) {
            CHECK_FALSE(has(body, "rx_ok"));
            CHECK_FALSE(has(body, "noise_dbm"));
        }
        if (has(body, "\"group\":\"radio\"")) {
            CHECK_FALSE(has(body, "firmware"));
            CHECK_FALSE(has(body, "\"baud\""));
        }
    }
}

// All or nothing, asked before the first frame goes out: a bench holding four
// frames of a five-frame dump, with no fifth one coming, has been told that a
// fault it cannot see is absent.
TEST_CASE("diagnostics: a link too narrow for one field is refused, not answered short") {
    const Diagnostics diagnostics = widest_device();
    DiagnosticsReport report(diagnostics, link::Command::Diagnostics);
    CHECK(report.fits(kSmallestSupportedPayloadBytes));
    // What BLE guarantees cannot carry the receiver's 23-character banner and the frame's keys.
    CHECK_FALSE(report.fits(20));

    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    DiagnosticsReport narrow(diagnostics, link::Command::Diagnostics);
    CHECK(narrow.next_frame(12, buffer, sizeof(buffer)) == 0);
    CHECK_FALSE(narrow.exhausted());
}

// The radio group is the widest a dump writes, each frame of the whole dump as of the radio reply.
TEST_CASE("diagnostics: the widest radio frame is 112 bytes, one frame on an iPhone's link") {
    for (link::Command command : {link::Command::Diagnostics, link::Command::Radio}) {
        DiagnosticsReport report(widest_device(), command, DiagnosticsReport::Group::Radio);
        uint8_t buffer[DiagnosticsReport::kFrameBytes];
        // 1 + four 2-byte heads + noise 3 + duty 4 + twelve counters of 6 + two of 4 + 2 + two
        // of 7.
        CHECK(report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer)) == 112);
        CHECK(report.exhausted());
    }
}

TEST_CASE("diagnostics: one subsystem can be asked for on its own, under its own name") {
    const Diagnostics diagnostics = busy_device();
    DiagnosticsReport report(diagnostics, link::Command::Radio, DiagnosticsReport::Group::Radio);
    CHECK(report.line_count() == 1);

    uint8_t buffer[DiagnosticsReport::kFrameBytes];
    std::string body;
    while (!report.exhausted()) {
        const int written =
            report.next_frame(kSmallestSupportedPayloadBytes, buffer, sizeof(buffer));
        REQUIRE(written > 0);
        body += text(buffer, written);
    }
    CHECK(has(body, "\"command\":\"radio\""));
    CHECK(has(body, "\"group\":\"radio\""));
    CHECK(has(body, "\"range_refused\":5"));
    // And nothing from any other subsystem came with it.
    CHECK_FALSE(has(body, "uptime_s"));
    CHECK_FALSE(has(body, "firmware"));
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
    CHECK(fields == 52);
}
