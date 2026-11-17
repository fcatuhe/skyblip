// Bravo plays the calls its pinned Go grades, with the Go's alarm, and grades its own without the
// Go.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "test/support/bravo_rig.h"
#include "test/support/log_transfer.h"
#include "test/support/paired_link.h"
#include "test/support/registry_frames.h"

using namespace skyblip;

namespace {

constexpr uint32_t kAircraft = 0x3C6586;
constexpr int32_t kSpeedMmS = 25000;
constexpr uint32_t kStepMs = 10;
constexpr events::BluetoothAddress kStranger{1, {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6}};

std::string sentence_field(const std::string& sentence, int index) {
    size_t at = 0;
    for (int i = 0; i < index; i++) at = sentence.find(',', at) + 1;
    return sentence.substr(at, sentence.find_first_of(",*", at) - at);
}

struct Call {
    std::string alarm{"loud"};
    std::string announcing{"none"};
    uint32_t announcement{0};
    std::string undismissed{"none"};
    bool paired{true};
};

Call advisory(uint32_t announcement, const char* alarm = "loud") {
    return Call{alarm, "advisory", announcement, "advisory", true};
}

std::string claim_json(const Call& call) {
    return complete_registry_frame(
        std::string("{\"command\":\"peer_claim\",\"boot_epoch\":7,\"paired\":") +
        (call.paired ? "true" : "false") + ",\"alarm\":\"" + call.alarm + "\",\"announcing\":\"" +
        call.announcing + "\",\"announcement\":" + std::to_string(call.announcement) +
        ",\"undismissed\":\"" + call.undismissed + "\"}");
}

struct Pinned {
    BravoRig rig{};
    PairedLink go{rig.platform.link()};
    PairedLink* stranger{nullptr};
    std::vector<std::string> to_go{};
    std::vector<std::string> to_stranger{};
    int32_t speed_mm_s{kSpeedMmS};

    Pinned() {
        REQUIRE(rig.setup() == Status::Ok);
        rig.product.settings().peer = bravo::pin_of(PairedLink::kCentralAddress);
        REQUIRE(go.connect());
    }

    void run_for(uint32_t span_ms) {
        for (uint32_t stepped = 0; stepped < span_ms; stepped += kStepMs) {
            rig.step();
            go.pump();
            if (stranger != nullptr) stranger->pump();
            rig.now_ms += kStepMs;
            for (std::string frame; go.take(frame);) to_go.push_back(link_text(frame));
            if (stranger != nullptr)
                for (std::string frame; stranger->take(frame);)
                    to_stranger.push_back(link_text(frame));
        }
    }

    void claim(const Call& call) { REQUIRE(go.write(claim_json(call)) == Status::Ok); }

    // One second of flight north: Bravo's fix, the Go's claim, the passes after.
    void second(const Call* call) {
        rig.push_fix(speed_mm_s);
        if (call != nullptr) claim(*call);
        run_for(1000);
        rig.utc_offset_s++;
    }

    void seconds(uint32_t count, const Call* call) {
        for (uint32_t i = 0; i < count; i++) second(call);
    }

    // An aircraft ahead, close enough that Bravo grades it an advisory on its own.
    void heard_ahead(const Call* call) {
        rig.hear_aircraft(kAircraft, 800);
        second(call);
        REQUIRE(rig.state().alarm_level == traffic::Level::Advisory);
    }

    uint32_t tones() { return rig.platform.annunciator().tone_commands(); }
    uint32_t motor_starts() { return rig.platform.annunciator().motor_starts(); }
    traffic::Level lamp_level() { return rig.state().alarm_undismissed; }

    static std::string last_health(const std::vector<std::string>& frames) {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (field(*it, "command") == "health") return *it;
        return std::string();
    }
};

const Call kQuiet{};

}  // namespace

TEST_CASE(
    "bravo follow: Bravo plays its Go's call, not its own grading, which NMEA still carries") {
    Pinned pinned;
    pinned.rig.raise_link(3);
    pinned.seconds(20, &kQuiet);
    const uint32_t tones = pinned.tones();
    pinned.heard_ahead(&kQuiet);
    pinned.seconds(2, &kQuiet);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.lamp_level() == traffic::Level::None);
    // FLARM's $PFLAU carries the alarm level fifth: the phone's own alarm follows Bravo's stream.
    CHECK(sentence_field(pinned.rig.last_of("$PFLAU"), 5) != "0");

    const Call call = advisory(1);
    pinned.claim(call);
    pinned.run_for(100);
    CHECK(pinned.tones() > tones);
    CHECK(pinned.motor_starts() > 0);
    CHECK(pinned.lamp_level() == traffic::Level::Advisory);
    CHECK(pinned.rig.state().followed_call.has_value());
}

TEST_CASE("bravo follow: a new announcement under a standing level is called again") {
    Pinned pinned;
    pinned.seconds(20, &kQuiet);
    Call call = advisory(1);
    pinned.second(&call);
    const uint32_t first = pinned.tones();
    REQUIRE(first > 0);
    pinned.seconds(3, &call);
    CHECK(pinned.tones() == first);

    call = advisory(2);
    pinned.second(&call);
    CHECK(pinned.tones() > first);
}

TEST_CASE("bravo follow: Bravo plays the Go's call at the Go's alarm, whatever its own says") {
    Pinned pinned;
    pinned.rig.product.settings().alarm = bravo::Alarm::Off;
    pinned.seconds(20, &kQuiet);

    const Call soft = advisory(1, "soft");
    pinned.second(&soft);
    CHECK(pinned.rig.platform.annunciator().loudness() == ports::Loudness::Soft);
    CHECK(pinned.lamp_level() == traffic::Level::Advisory);
    CHECK(pinned.rig.product.settings().go_alarm == bravo::Alarm::Soft);
    CHECK(pinned.rig.product.settings().alarm == bravo::Alarm::Off);

    const uint32_t tones = pinned.tones();
    const Call visual = advisory(2, "visual");
    pinned.second(&visual);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.lamp_level() == traffic::Level::Advisory);
    CHECK(pinned.rig.lamp() == indication::Condition::Alarm);

    Call off = advisory(3, "off");
    off.undismissed = "none";
    pinned.second(&off);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.lamp_level() == traffic::Level::None);
    CHECK(pinned.rig.lamp() != indication::Condition::Alarm);
}

// A press on Bravo silences it at once, and its Go hears which call it dismissed.
TEST_CASE("bravo follow: a press dismisses the Go's call at once and tells the Go in health") {
    Pinned pinned;
    pinned.seconds(20, &kQuiet);
    const Call call = advisory(5);
    pinned.second(&call);
    REQUIRE(pinned.lamp_level() == traffic::Level::Advisory);
    CHECK(field(Pinned::last_health(pinned.to_go), "dismissed").empty());

    const size_t sent = pinned.to_go.size();
    pinned.rig.platform.set_button_down(true);
    pinned.run_for(150);
    pinned.rig.platform.set_button_down(false);
    pinned.run_for(150);
    CHECK(pinned.lamp_level() == traffic::Level::None);
    CHECK_FALSE(pinned.rig.platform.annunciator().sounding());
    const std::vector<std::string> after(pinned.to_go.begin() + static_cast<long>(sent),
                                         pinned.to_go.end());
    CHECK(field(Pinned::last_health(after), "dismissed") == "5");

    const uint32_t tones = pinned.tones();
    pinned.seconds(2, &call);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.lamp_level() == traffic::Level::None);

    const Call next = advisory(6);
    pinned.second(&next);
    CHECK(pinned.tones() > tones);
    CHECK(pinned.lamp_level() == traffic::Level::Advisory);
}

TEST_CASE("bravo follow: with its Go's link down, Bravo grades its own ADS-B at the Go's alarm") {
    Pinned pinned;
    const Call soft{"soft"};
    pinned.seconds(20, &soft);
    REQUIRE(pinned.rig.product.alarm().alarm_in_force() == bravo::Alarm::Soft);

    pinned.go.drop();
    pinned.run_for(100);
    CHECK_FALSE(pinned.rig.state().followed_call.has_value());
    const uint32_t tones = pinned.tones();
    pinned.heard_ahead(nullptr);
    CHECK(pinned.tones() > tones);
    CHECK(pinned.rig.platform.annunciator().loudness() == ports::Loudness::Soft);
    CHECK(pinned.lamp_level() == traffic::Level::Advisory);
}

// One lost claim keeps Bravo on its Go's calls, and three claim periods without one give it its
// own.
TEST_CASE("bravo follow: claims that stop on a link still up give Bravo its own grading back") {
    Pinned pinned;
    pinned.seconds(20, &kQuiet);
    const uint32_t tones = pinned.tones();
    pinned.heard_ahead(&kQuiet);
    REQUIRE(pinned.tones() == tones);

    pinned.run_for(bravo::PeerService::kCallMaxAgeMs - 1000 - 100);
    CHECK(pinned.tones() == tones);
    CHECK(pinned.rig.state().followed_call.has_value());
    pinned.run_for(200);
    CHECK(pinned.tones() > tones);
    CHECK_FALSE(pinned.rig.state().followed_call.has_value());
}

TEST_CASE("bravo follow: a Go that unpairs gives Bravo back its own alarm and its own grading") {
    Pinned pinned;
    pinned.rig.product.settings().alarm = bravo::Alarm::Visual;
    const Call loud{"loud"};
    pinned.seconds(20, &loud);
    REQUIRE(pinned.rig.product.alarm().alarm_in_force() == bravo::Alarm::Loud);

    Call farewell = loud;
    farewell.paired = false;
    pinned.claim(farewell);
    pinned.run_for(100);
    CHECK_FALSE(pinned.rig.product.peer().paired());
    CHECK_FALSE(pinned.rig.state().followed_call.has_value());
    CHECK(pinned.rig.product.alarm().alarm_in_force() == bravo::Alarm::Visual);
    CHECK(pinned.rig.product.settings().alarm == bravo::Alarm::Visual);
}

TEST_CASE("bravo follow: a claim from a Go Bravo has not pinned sets neither its call nor alarm") {
    Pinned pinned;
    PairedLink stranger{pinned.rig.platform.link(), 2, kStranger};
    pinned.stranger = &stranger;
    REQUIRE(stranger.connect());
    const Call quiet_soft{"soft"};
    for (int i = 0; i < 20; i++) {
        REQUIRE(stranger.write(claim_json(advisory(1, "off"))) == Status::Ok);
        pinned.second(&quiet_soft);
    }
    CHECK(pinned.rig.product.settings().go_alarm == bravo::Alarm::Soft);
    CHECK(pinned.lamp_level() == traffic::Level::None);

    pinned.rig.platform.set_button_down(true);
    pinned.run_for(150);
    pinned.rig.platform.set_button_down(false);
    pinned.run_for(1500);
    CHECK(field(Pinned::last_health(pinned.to_stranger), "dismissed").empty());
}

TEST_CASE("bravo follow: the Go's last alarm comes back from flash after Bravo's reboot") {
    platform::host::MeshtrackerX1 platform;
    {
        Bravo first{platform};
        REQUIRE(first.setup() == Status::Ok);
        first.settings().peer = bravo::pin_of(PairedLink::kCentralAddress);
        first.settings().go_alarm = bravo::Alarm::Soft;
        first.config_link().config().note_settings_changed();
        first.config_link().flush_settings(1000);
    }
    Bravo second{platform};
    REQUIRE(second.setup() == Status::Ok);
    CHECK(second.settings().go_alarm == bravo::Alarm::Soft);
    CHECK(second.settings().alarm == bravo::Alarm::Loud);
    CHECK(second.alarm().alarm_in_force() == bravo::Alarm::Soft);
}

